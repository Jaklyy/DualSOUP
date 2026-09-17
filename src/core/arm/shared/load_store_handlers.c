#include "core/scheduler.h"
#include "inc.h"



void ARM_STR(ARM* cpu, u32 addr, u8 rd, bool priv, u8 rn, u32 wbaddr, u32 baserestore, bool writeback, ARM_DataWidth size)
{
    u32 wrdata = ARM_GetReg(rd); // Rd is fetched before base writeback

    if (size == ARMDataWidth_8)
    {
        wrdata &= 0xFF;
        wrdata |= (wrdata << 8) | (wrdata << 16) | (wrdata << 24);
    }
    else if (size == ARMDataWidth_16)
    {
        wrdata &= 0xFFFF;
        wrdata |= (wrdata << 16);
    }

    if (cpu->CPUID == ARM7ID)
    {
        ARM7TDMI* a7tdmi = ARM7Cast;
        // note: for some reason str doesn't get affected by the weird nonsense ldr does on arm7tdmi when using pc as base
        if (writeback) ARM_SetReg(rn, wbaddr);

        // schedule store
        A7TDMI_PostMem pass = {
            .WrData = {[0]=wrdata},
            .Addr = addr,
            .RListOrig = 1<<rd,
            .RBase = 0,
            .NumFetch = 1,
            .NumFetchCompleted = 0,
            .Size = size,
            .SignExt = false,
            .Priv = priv,
            .DataCB = A7TDMIDataCB_StoreSingle,
        };
        a7tdmi->PostMem = pass;
        A7TDMI_DataWrite(a7tdmi, cpu->Timestamp);
    }
    else // arm9e-s
    {
        ARM946ES* a9es = ARM9Cast;
        u8 base;
        // base writeback doesn't work for pc on arm9e-s
        if (writeback && (rn != 15))
        {
            A9ES_SetReg(a9es, rn, wbaddr);
            base = rn;
        }
        else base = u8_max;

        s8 ildelay = A9ES_TestTwoCycleInterlocks(a9es);

        // schedule store
        A9ES_PostMem pass = {
            .WrData = {[0]=wrdata},
            .Addr = addr,
            .BaseRestore = baserestore,
            .Rd = 0, // unused
            .RBase = base,
            .DataAbort = false,
            .Size = size,
            .SignExt = false, // unused
            .Priv = priv,
            .ILDelay = ildelay,
            .ILRetry = false, // unused
            .Write = true,
            .DataCB = A9ESDataCB_StoreSingle,
            .SubmMax = 1,
            .SubmCur = 0,
            .CompCur = 0,
            .InstrPtr = 0, // unused
        };
        A9ES_DataGo(a9es, &pass);
        if (!ildelay) A9ES_InstrGo(a9es, false);
    }
}

void A7TDMI_STR_Post(ARM7TDMI* a7tdmi)
{
    a7tdmi->ARM.CodeSeq = false;
    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp);
}

void A9ES_STR_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore); // restore previous value of base (base restored abort model)
        A9ES_InstrGo(a9es, false);
        return A9ES_DataAbort(a9es);
    }

    if (pass->ILDelay) // interlock condition was detected; clean up
        A9ES_InstrGo(a9es, false); // schedule fetch; cycle addition useless because it's always 1 cycle
}

void ARM_LDR(ARM* cpu, u32 addr, u8 rd, bool priv, u8 rn, u32 wbaddr, u32 baserestore, bool writeback, ARM_DataWidth size, bool signext)
{
    if (cpu->CPUID == ARM7ID)
    {
        ARM7TDMI* a7tdmi = ARM7Cast;
        if (writeback)
        {
            if (rn == 15)
            {
                // it's always fun when an "unpredictable" instruction encoding does something that leaves you genuinely flabbergasted.
                // the address used for the load is still +8 but the writeback value is +12 for some reason...?
                // the loaded value is also not properly written back afterwards for some reason
                //      my best guess is that the "Rd writeback cycle" gets overridden by the pipeline refill cycles
                //      i'm currently speculating the cpu uses a form of microcode internally, and encoding a list of things to do on each cycle of an instruction
                //      and the cycle that should writeback the load gets replaced by the base writeback's pipeline refill cycles due to an "oversight" in decoding (in quotes since this behavior is already out of spec)
                //      so this part actually kinda makes sense i think
                // this only applies to Rn and not Rm
                // and only happens for loads and not stores
                // and the two above facts make me so fucking confused because there's no obvious reason why ldr and str would handle base writeback differently???
                // maybe it has something to do with address pipelining???????????
                // btw this is (probably) not the correct way to emulate this. but the correct way to do this is probably stupid, and this works correctly for this specific edge case at least.
                // some insight might be able to be gained via ldm user bank quirks? since i believe those bug out reg reads only on the first cycle of an instruction? or at least that's how it works on gba...?
                wbaddr += 4;
            }
            A7TDMI_SetReg(a7tdmi, rn, wbaddr);
        }

        // schedule load
        A7TDMI_PostMem pass = {
            .RData = {},
            .Addr = addr,
            .RListOrig = 1<<rd,
            .RBase = (writeback) ? rn : u8_max,
            .NumFetch = 1,
            .NumFetchCompleted = 0,
            .Size = size,
            .SignExt = signext,
            .Priv = priv,
            .DataCB = A7TDMIDataCB_LoadSingle,
        };
        a7tdmi->PostMem = pass;
        A7TDMI_DataRead(a7tdmi, cpu->Timestamp);
    }
    else
    {
        ARM946ES* a9es = ARM9Cast;
        u8 base;
        // base writeback doesn't work for pc on arm9e-s
        if (writeback && (rn != 15))
        {
            A9ES_SetReg(a9es, rn, wbaddr);
            base = rn;
        }
        else base = u8_max;

        // test rd interlocks; has to be done now so we can know if an instruction can occur in sync with the load. (this might not actually matter? it is more similar to hw so...)
        u8 illen = 1+((size != ARMDataWidth_32) || (addr & 3));
        s8 ildelay;
        bool retry = false;
        if (rd == 15) ildelay = illen; // pc always interlocks
        else ildelay = A9ES_DecodeInterlocks(a9es, cpu->CPSR.Thumb, rd, illen, 2, &retry);

        if (!ildelay)
        {
            ildelay = A9ES_TestTwoCycleInterlocks(a9es);
            if (ildelay) retry = false;
        }

        // schedule load
        A9ES_PostMem pass = {
            .RData = {},
            .Addr = addr,
            .BaseRestore = baserestore,
            .Rd = rd,
            .RBase = base,
            .DataAbort = false,
            .Size = size,
            .SignExt = signext,
            .Priv = priv,
            .ILDelay = ildelay,
            .ILRetry = retry,
            .Write = false,
            .DataCB = A9ESDataCB_LoadSingle,
            .SubmMax = 1,
            .SubmCur = 0,
            .CompCur = 0,
            .InstrPtr = 0, // unused
        };
        A9ES_DataGo(a9es, &pass);
        // if interlock case detected delay fetch
        if (!ildelay) A9ES_InstrGo(a9es, false);
    }
}

void A9ES_LDR_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore); // restore previous value of base (base restored abort model)
        A9ES_InstrGo(a9es, false);
        return A9ES_DataAbort(a9es);
    }

    u32 rdata = pass->RData[0];
    u8 rd = pass->Rd;
    A9ES_RotateExtendUnit(&rdata, pass->Addr, pass->Size, pass->SignExt, a9es->CP15.CR.BigEndian);

    // loads can interwork on arm9 when the disable bit is clear.
    if ((rd == 15) && !a9es->CP15.CR.NoLoadTBit)
        ARM_SetThumb(&a9es->ARM, rdata & 1);

    A9ES_SetReg(a9es, rd, rdata);

    if (pass->ILDelay) // interlock condition was detected; clean up
    {
        A9ES_ExecuteCycles(a9es, pass->ILDelay-1);
        A9ES_InstrGo(a9es, false); // schedule fetch
    }
    else if (pass->ILRetry)
        A9ES_SetTwoCycleInterlock(a9es, rd);
}

void A7TDMI_LDR_Post(ARM7TDMI* a7tdmi)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;

    if (pass->RBase == 15) // buggy; skip wb cycle
    {
        printf("weird bullshit opcode\n");
        a7tdmi->ARM.CodeSeq = false; // checkme
        return A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp);
    }

    u32 rdata = pass->RData[0];
    u8 rd = stdc_trailing_zeros(pass->RListOrig);
    A7TDMI_RotateExtendUnit(&rdata, pass->Addr, pass->Size, pass->SignExt);

    A7TDMI_SetReg(a7tdmi, rd, rdata);

    a7tdmi->ARM.CodeSeq = false;
    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp+DSClk33(1));
}

void ARM_STM(ARM* cpu, u32 addr, u16 rlist, u32 wbaddr, u32 baserestore, u8 rn, bool writeback, bool special)
{
    if (cpu->CPUID == ARM7ID)
    {
        ARM7TDMI* a7tdmi = ARM7Cast;

        A7TDMI_PostMem pass = {
            .WrData = {},
            .Addr = addr,
            .NumFetch = stdc_count_ones(rlist),
            .NumFetchCompleted = 0,
            .Size = ARMDataWidth_32,
            .Priv = cpu->Privileged,
            .DataCB = A7TDMIDataCB_StoreMultiple,
        };

        u8 oldmode = a7tdmi->ARM.CPSR.Mode;
        if (special) ARM_SetMode(&a7tdmi->ARM, ARMMode_USR); // user regs stm; hacky

        // add fetched words
        u16 rlisttmp = rlist;
        u8 count = 0;

        if (rlisttmp)
        {
            u8 reg = stdc_trailing_zeros((u32)rlisttmp);
            rlisttmp &= (~1)<<reg;
            pass.WrData[count++] = A7TDMI_GetReg(a7tdmi, reg);
        }

        // ARM7TDMI performs base writeback after the first store register was fetched
        if (writeback && (rn < 15)) A7TDMI_SetReg(a7tdmi, rn, wbaddr); // CHECKME: user regs?

        while (rlisttmp)
        {
            u8 reg = stdc_trailing_zeros((u32)rlisttmp);
            rlisttmp &= (~1)<<reg;
            pass.WrData[count++] = A7TDMI_GetReg(a7tdmi, reg);
        }

        if (special) ARM_SetMode(&a7tdmi->ARM, oldmode); // user regs stm; hacky

        a7tdmi->PostMem = pass;
        A7TDMI_DataWrite(a7tdmi, cpu->Timestamp);
    }
    else
    {
        ARM946ES* a9es = ARM9Cast;
        u8 base = ((writeback) ? rn : u8_max);

        // INTERLOCK NOTES:
        // single reg case is not actually an interlock, but it functions similarly enough in practice
        // two cycle interlocks dont need to be tested since stm is always at least 2 cycles long

        A9ES_PostMem pass = {
            .WrData = {},
            .Addr = addr,
            .BaseRestore = baserestore,
            .RListOrig = rlist,
            .RListRem = 0, // unused
            .RBase = base,
            .DataAbort = false,
            .Size = ARMDataWidth_32,
            .Special = special,
            .Priv = cpu->Privileged,
            .ILDelay = (stdc_count_ones(rlist) <= 1), // see above interlock notes
            .Write = true,
            .DataCB = A9ESDataCB_StoreMultiple,

            .SubmMax = stdc_count_ones(rlist),
            .SubmCur = 0,
            .CompCur = 0,
            .InstrPtr = 0,
        };

        u8 oldmode = a9es->ARM.CPSR.Mode;
        if (special) ARM_SetMode(&a9es->ARM, ARMMode_USR); // user regs stm; hacky

        // add fetched words
        u16 rlisttmp = rlist;
        u8 count = 0;
        while (rlisttmp)
        {
            u8 reg = stdc_trailing_zeros((u32)rlisttmp);
            rlisttmp &= (~1)<<reg;
            pass.WrData[count++] = A9ES_GetReg(a9es, reg);
        }

        if (special) ARM_SetMode(&a9es->ARM, oldmode); // user regs stm; hacky

        // ARM9E-S performs base writeback after the last store register was fetched
        if (base < 15) A9ES_SetReg(a9es, base, wbaddr);

        if (stdc_count_ones(rlist) == 0) // checkme
        {
            a9es->PostMem = pass;
            A9ES_DataDone(a9es);
            // instr handled in post handler
            return;
        }

        A9ES_DataGo(a9es, &pass);
        if (!pass.ILDelay) A9ES_InstrGo(a9es, true);
    }
}

void A9ES_STM_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;

    // burst incomplete; schedule remainder
    if (pass->SubmCur != pass->SubmMax)
    {
        pass->Addr += (pass->SubmCur - pass->InstrPtr) * 4;
        pass->InstrPtr = pass->SubmCur;
        A9ES_DataGo(a9es, &a9es->PostMem);
        if (!pass->ILDelay) // this if is likely unneeded
            A9ES_InstrGo(a9es, true);
        return;
    }

    // burst completed; handle cleanup
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore);
        A9ES_InstrGo(a9es, false);
        return A9ES_DataAbort(a9es);
    }

    if (pass->ILDelay) // not actually an interlock but w/e; its a 1 cycle delay so...
        A9ES_InstrGo(a9es, false);
}

void A7TDMI_STM_Post(ARM7TDMI* a7tdmi)
{
    a7tdmi->ARM.CodeSeq = false;
    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp);
}

void ARM_LDM(ARM* cpu, u32 addr, u16 rlist, u32 wbaddr, u32 baserestore, u8 rn, bool writeback, bool special)
{
    if (cpu->CPUID == ARM7ID)
    {
        ARM7TDMI* a7tdmi = ARM7Cast;
        u8 base;
        if (writeback && (rn != 15))
        {
            A7TDMI_SetReg(a7tdmi, rn, wbaddr);
            base = rn;
        }
        else base = u8_max;

        A7TDMI_PostMem pass = {
            .RData = {},
            .Addr = addr,
            .RListOrig = rlist, // checkme: empty rlist?
            .RListRem = rlist,
            .RBase = base,
            .NumFetch = stdc_count_ones(rlist),
            .NumFetchCompleted = 0,
            .Size = ARMDataWidth_32,
            .Special = special,
            .Priv = cpu->Privileged,
            .DataCB = A7TDMIDataCB_LoadMultiple,
        };
        a7tdmi->PostMem = pass;
        A7TDMI_DataRead(a7tdmi, cpu->Timestamp);
    }
    else
    {
        ARM946ES* a9es = ARM9Cast;
        u8 base;
        // checkme: base writeback pc on arm9e-s?
        if (writeback && (rn != 15))
        {
            A9ES_SetReg(a9es, rn, wbaddr);
            base = rn;
        }
        else base = u8_max;

        u8 rmax = (15+16) - stdc_leading_zeros((u32)rlist);
        s8 ildelay;
        bool retry = false;
        // pc always interlocks
        // NOTE: single reg case is not actually an interlock, but it functions similarly enough in practice
        if ((rmax == 15) || (stdc_count_ones(rlist) <= 1)) ildelay = 1;
        else ildelay = A9ES_DecodeInterlocks(a9es, cpu->CPSR.Thumb, rmax, 1, 2, &retry);
        // two cycle interlocks dont need to be tested since ldm is always at least 2 cycles long

        A9ES_PostMem pass = {
            .RData = {},
            .Addr = addr,
            .BaseRestore = baserestore,
            .RListOrig = rlist, // checkme: empty rlist?
            .RListRem = rlist,
            .RBase = base,
            .DataAbort = false,
            .Size = ARMDataWidth_32,
            .Special = special,
            .Priv = cpu->Privileged,
            .ILDelay = ildelay,
            .ILRetry = retry,
            .Write = false,
            .DataCB = A9ESDataCB_LoadMultiple,

            .SubmMax = stdc_count_ones(rlist),
            .SubmCur = 0,
            .CompCur = 0,
            .InstrPtr = 0,
        };

        if (stdc_count_ones(rlist) == 0) // checkme
        {
            a9es->PostMem = pass;
            A9ES_DataDone(a9es);
            return;
        }

        A9ES_DataGo(a9es, &pass);
        if (!ildelay) A9ES_InstrGo(a9es, true);
    }
}

void A9ES_LDM_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;

    // process completed loads
    if (!pass->DataAbort)
    {
        u8 oldmode = a9es->ARM.CPSR.Mode;
        if (pass->Special && !(pass->RListOrig >> 15)) // user regs ldm; hacky
            ARM_SetMode(&a9es->ARM, ARMMode_USR);

        for (u8 i = pass->InstrPtr; i< pass->SubmCur; i++)
        {
            u8 reg = stdc_trailing_zeros((u32)pass->RListRem);
            pass->RListRem &= (~1)<<reg;

            u32 rdata = pass->RData[i];
            // update cpsr
            if (reg == 15)
            {
                if (pass->Special) // exception return ldm; (overrides normal load interworking)
                    ARM_SetCPSR(&a9es->ARM, A9ES_GetSPSR(a9es).Raw);
                else if (!a9es->CP15.CR.NoLoadTBit) // load interworking
                    ARM_SetThumb(&a9es->ARM, rdata & 1);
            }

            // base writeback is done after the second to last load is written back
            // so a loaded value will be overwritten by base writeback unless it is the last value loaded
            if ((pass->RBase >= 15) || (pass->RBase != reg) // no writeback or not the base
            || ((i >= (pass->SubmMax-1)) && (pass->SubmMax > 1))) // writeback has already occured
                A9ES_SetReg(a9es, reg, rdata);
        }

        if (pass->Special && !(pass->RListOrig >> 15)) // user regs ldm; hacky
            ARM_SetMode(&a9es->ARM, oldmode);
    }

    // burst incomplete; schedule remainder
    if (pass->SubmCur != pass->SubmMax)
    {
        pass->Addr += (pass->SubmCur - pass->InstrPtr) * 4;
        pass->InstrPtr = pass->SubmCur;
        A9ES_DataGo(a9es, &a9es->PostMem);
        if (!pass->ILDelay) A9ES_InstrGo(a9es, true);
        return;
    }

    // burst completed; handle cleanup
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore);
        A9ES_InstrGo(a9es, false);
        return A9ES_DataAbort(a9es);
    }

    if (pass->ILDelay)
    {
        A9ES_ExecuteCycles(a9es, pass->ILDelay-1);
        A9ES_InstrGo(a9es, false);
    }
}

void A7TDMI_LDM_Post(ARM7TDMI* a7tdmi)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;

    u8 oldmode = a7tdmi->ARM.CPSR.Mode;
    if (pass->Special && !(pass->RListOrig >> 15)) // user regs ldm; hacky
        ARM_SetMode(&a7tdmi->ARM, ARMMode_USR);

    for (u8 i = 0; i < pass->NumFetchCompleted; i++)
    {
        u8 reg = stdc_trailing_zeros((u32)pass->RListRem);
        pass->RListRem &= (~1)<<reg;

        u32 rdata = pass->RData[i];
        // update cpsr
        if ((reg == 15) && pass->Special) // exception return ldm
                ARM_SetCPSR(&a7tdmi->ARM, A7TDMI_GetSPSR(a7tdmi).Raw);

        // base writeback is done before the first load is written back, so no handling is needed
        A7TDMI_SetReg(a7tdmi, reg, rdata);
    }

    if (pass->Special && !(pass->RListOrig >> 15)) // user regs ldm; hacky
        ARM_SetMode(&a7tdmi->ARM, oldmode);

    a7tdmi->ARM.CodeSeq = false;
    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp+DSClk33(1));
}

void A9ES_SWPLoad_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort) // store does not occur if load was aborted
    {
        A9ES_InstrGo(a9es, false);
        return A9ES_DataAbort(a9es);
    }

    // schedule store

    // hacky way to keep store data in index 1 and read data in index 0
    pass->SubmMax = 2;
    pass->SubmCur = 1;
    pass->CompCur = 1; // CHECKME?
    pass->Write = true;
    pass->DataCB = A9ESDataCB_SwapStore;
    A9ES_DataGo(a9es, &a9es->PostMem);
    // cursed note: an itcm instr load can be run between the load and store of a swp
    if (!pass->ILDelay) A9ES_InstrGo(a9es, false);
}

void A9ES_SWPStore_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort) // mission failed
    {
        A9ES_InstrGo(a9es, false);
        return A9ES_DataAbort(a9es);
    }

    // load writeback occurs now
    u32 rdata = pass->RData[0];
    u8 rd = pass->Rd;
    A9ES_RotateExtendUnit(&rdata, pass->Addr, pass->Size, false, a9es->CP15.CR.BigEndian);

    // loads can interwork on arm9 when the disable bit is clear.
    if ((rd == 15) && !a9es->CP15.CR.NoLoadTBit)
        ARM_SetThumb(&a9es->ARM, rdata & 1);

    A9ES_SetReg(a9es, rd, rdata);

    if (pass->ILDelay) // interlock condition was detected; clean up
    {
        A9ES_ExecuteCycles(a9es, pass->ILDelay-1);
        A9ES_InstrGo(a9es, false); // schedule fetch
    }
    else if (pass->ILRetry)
        A9ES_SetTwoCycleInterlock(a9es, rd);
}

void A7TDMI_SWPLoad_Post(ARM7TDMI* a7tdmi)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;

    // schedule store

    // hacky way to keep store data in index 1 and read data in index 0
    pass->NumFetch = 2;
    pass->NumFetchCompleted = 1;
    pass->DataCB = A7TDMIDataCB_SwapStore;
    A7TDMI_DataWrite(a7tdmi, a7tdmi->ARM.Timestamp);
}

void A7TDMI_SWPStore_Post(ARM7TDMI* a7tdmi)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;

    // load writeback occurs now
    u32 rdata = pass->RData[0];
    u8 rd = stdc_trailing_zeros(pass->RListOrig);
    A7TDMI_RotateExtendUnit(&rdata, pass->Addr, pass->Size, false);

    A7TDMI_SetReg(a7tdmi, rd, rdata);

    a7tdmi->ARM.CodeSeq = false;
    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp+DSClk33(1));
}