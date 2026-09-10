#include <stdbit.h>
#include <stdckdint.h>
#include "core/scheduler.h"
#include "core/arm/arm7/arm.h"
#include "core/arm/arm9/arm.h"
#include "core/utils.h"
#include "arm.h"
#include "inc.h"


u32 ARM_ADD(const u32 rn_val, const u32 shifter_out, ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = ckd_add((u32*)&alu_out, (u32)rn_val, (u32)shifter_out);
    flags_out->Overflow = ckd_add((s32*)&alu_out, (s32)rn_val, (s32)shifter_out);
    return alu_out;
}

u32 ARM_ADC(const u32 rn_val, const u32 shifter_out, const bool carry_in, ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    ARM_FlagsOut flags[2]; // this can be uninitialized because this instruction sets all flags
    alu_out = ARM_ADD(rn_val, shifter_out, &flags[0]);
    alu_out = ARM_ADD(alu_out, carry_in, &flags[1]);
    flags_out->Raw = flags[0].Raw | flags[1].Raw;
    return alu_out;
}

u32 ARM_SUB_RSB(const u32 a, const u32 b, ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = !ckd_sub((u32*)&alu_out, (u32)a, (u32)b);
    flags_out->Overflow =  ckd_sub((s32*)&alu_out, (s32)a, (s32)b);
    return alu_out;
}

u32 ARM_SBC_RSC(const u32 a, const u32 b, const bool carry_in, ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    ARM_FlagsOut flags[2]; // this can be uninitialized because this instruction sets all flags
    alu_out = ARM_SUB_RSB(a, b, &flags[0]);
    alu_out = ARM_SUB_RSB(alu_out, !carry_in, &flags[1]);
    flags_out->Carry = flags[0].Carry & flags[1].Carry;
    flags_out->Overflow = flags[0].Overflow | flags[1].Overflow;
    return alu_out;
}


u32 ARM_LSL(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        if (shift < 64)
        {
            val <<= shift;
            *carry_out = val & ((u64)1 << 32);
        }
        else
        {
            val = 0;
            *carry_out = 0;
        }
    }
    return val;
}

u32 ARM_LSR(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        if (shift < 64)
        {
            *carry_out = val & (1<<(shift-1));
            val >>= shift;
        }
        else
        {
            val = 0;
            *carry_out = 0;
        }
    }
    return val;
}

u32 ARM_ASR(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        if (shift < 64)
        {
            // sign extend
            *carry_out = (((s64)(s32)val) >> (shift-1)) & 1;
            val = ((s64)(s32)val) >> shift;
        }
        else
        {
            val = (s32)val >> 31;
            *carry_out = val;
        }
    }
    return val;
}

u32 ARM_ROR(u32 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        val = ROR32(val, shift);
        *carry_out = val & (1<<31);
    }
    return val;
}

u8 A7TDMI_NumBoothIters(const u32 rs_val, const bool signedcheck)
{
    u8 iter = stdc_leading_zeros(rs_val);
    // signed long (and all 32 bit) multiplications use signed early termination
    if (signedcheck) iter |= stdc_leading_ones(rs_val);
    iter = 4 - (iter / 8);

    if (iter < 1) iter = 1;
    return iter;
}

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
            .WrData = {[0] = wrdata},
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
            ARM_SetReg(rn, wbaddr);
            base = rn;
        }
        else base = u8_max;

        s8 ildelay = A9ES_TestTwoCycleInterlocks(a9es);

        // schedule store
        A9ES_PostMem pass = {
            .WrData = {[0]=wrdata},
            .Addr = addr,
            .BaseRestore = baserestore,
            .RListOrig = 1<<rd,
            .RBase = base,
            .DataAbort = false,
            .NumFetch = 1,
            .NumFetchCompleted = 0,
            .Size = size,
            .SignExt = false,
            .Priv = priv,
            .ILDelay = ildelay,
            .DataCB = A9ESDataCB_StoreSingle,
        };
        A9ES_DataGo(a9es, &pass);
        if (!ildelay) A9ES_InstrGo(a9es, false);
    }
}

void A7TDMI_STR_Post(ARM7TDMI* a7tdmi)
{
    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp);
}

void A9ES_STR_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore); // restore previous value of base (base restored abort model)
        A9ES_DataAbort(a9es);
        return;
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
                //      i'm currently speculating the cpu uses a form of microcode internally, and encoding a list of things to do one each cycle of an instruction
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
            .RListOrig = 1<<rd,
            .RBase = base,
            .DataAbort = false,
            .NumFetch = 1,
            .NumFetchCompleted = 0,
            .Size = size,
            .SignExt = signext,
            .Priv = priv,
            .ILDelay = ildelay,
            .ILRetry = retry,
            .DataCB = A9ESDataCB_LoadSingle,
        };
        A9ES_DataGo(a9es, &pass);
        // if interlock case detected delay fetch
        if (ildelay == 0) A9ES_InstrGo(a9es, false);
    }
}

void A9ES_LDR_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore); // restore previous value of base (base restored abort model)
        A9ES_DataAbort(a9es);
        return;
    }

    u32 rdata = pass->RData[0];
    u8 rd = stdc_trailing_zeros(pass->RListOrig);
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
        return A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp);

    u32 rdata = pass->RData[0];
    u8 rd = stdc_trailing_zeros(pass->RListOrig);
    A7TDMI_RotateExtendUnit(&rdata, pass->Addr, pass->Size, pass->SignExt);

    A7TDMI_SetReg(a7tdmi, rd, rdata);

    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp+DSClk33(1));
}

#if 0
void A7TDMI_LDR_Post(ARM7TDMI* a7tdmi)
{
    bool ilext = A7TDMI_RotateExtendUnit(&rdata, addr, size, signext);
    A9ES_SetReg(a9es, rd, rdata);

    s8 il = A9ES_DecodeInterlocks(a9es, false, rd, 1+ilext, 2);
    if (il) A9ES_SetTwoCycleInterlock(a9es, rd);
}
#endif

void ARM_STM(ARM* cpu, u32 addr, u16 rlist, u32 wbaddr, u32 baserestore, u8 rn, bool writeback, bool special)
{
    if (cpu->CPUID == ARM7ID)
    {
        ARM7TDMI* a7tdmi = ARM7Cast;
        u8 base = ((writeback) ? rn : u8_max);

        // INTERLOCK NOTES:
        // single reg case is not actually an interlock, but it functions similarly enough in practice
        // two cycle interlocks dont need to be tested since stm is always at least 2 cycles long

        A7TDMI_PostMem pass = {
            .WrData = {},
            .Addr = addr,
            .RListOrig = rlist,
            .RBase = base,
            .NumFetch = stdc_count_ones(rlist),
            .NumFetchCompleted = 0,
            .Size = ARMDataWidth_32,
            .Special = special,
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
        if (base < 15) A7TDMI_SetReg(a7tdmi, base, wbaddr); // CHECKME: user regs?

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
            .RBase = base,
            .DataAbort = false,
            .NumFetch = stdc_count_ones(rlist),
            .NumFetchCompleted = 0,
            .Size = ARMDataWidth_32,
            .Special = special,
            .Priv = cpu->Privileged,
            .ILDelay = (stdc_count_ones(rlist) <= 1), // see above interlock notes
            .DataCB = A9ESDataCB_StoreMultiple,
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

        A9ES_DataGo(a9es, &pass);
        if (!pass.ILDelay) A9ES_InstrGo(a9es, true);
    }
}

void A9ES_STM_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;

    // burst incomplete; schedule remainder
    if (pass->NumFetch != pass->NumFetchCompleted)
    {
        A9ES_DataGo(a9es, &a9es->PostMem);
        A9ES_InstrGo(a9es, true);
        return;
    }

    // burst completed; handle cleanup
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore);
        return A9ES_DataAbort(a9es);
    }

    if (pass->ILDelay) // not actually an interlock but w/e; its a 1 cycle delay so...
        A9ES_InstrGo(a9es, false);
}

void A7TDMI_STM_Post(ARM7TDMI* a7tdmi)
{
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
            .NumFetch = stdc_count_ones(rlist),
            .NumFetchCompleted = 0,
            .Size = ARMDataWidth_32,
            .Special = special,
            .Priv = cpu->Privileged,
            .ILDelay = ildelay,
            .ILRetry = retry,
            .DataCB = A9ESDataCB_LoadMultiple,
        };
        A9ES_DataGo(a9es, &pass);
        A9ES_InstrGo(a9es, true);
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

        for (u8 i = pass->NumFetchCompleted; i < pass->NumFetch; i++)
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
            || ((i == pass->NumFetch) && (pass->NumFetch != 1))) // writeback has already occured
                A9ES_SetReg(a9es, reg, rdata);
        }

        if (pass->Special && !(pass->RListOrig >> 15)) // user regs ldm; hacky
            ARM_SetMode(&a9es->ARM, oldmode);
    }

    // burst incomplete; schedule remainder
    if (pass->NumFetch != pass->NumFetchCompleted)
    {
        A9ES_DataGo(a9es, &a9es->PostMem);
        A9ES_InstrGo(a9es, true);
        return;
    }

    // burst completed; handle cleanup
    if (pass->DataAbort)
    {
        if (pass->RBase < 15) A9ES_SetReg(a9es, pass->RBase, pass->BaseRestore);
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

    for (u8 i = pass->NumFetchCompleted; i < pass->NumFetch; i++)
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

    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp+DSClk33(1));
}
