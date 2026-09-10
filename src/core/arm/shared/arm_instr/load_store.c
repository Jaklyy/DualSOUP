#include "core/arm/arm7/arm.h"
#include "core/scheduler.h"
#include "core/utils.h"
#include "../arm.h"
#include "../inc.h"
#include "../../arm9/arm.h"



union ARM_LoadStore_Decode
{
    u32 Raw;
    struct
    {
        u32 Imm12 : 12;
        u32 Rd : 4;
        u32 Rn : 4;
        bool Load : 1;
        bool Writeback : 1;
        bool Byte : 1;
        bool Up : 1;
        bool PreIndex : 1;
        bool Register : 1;
    };
    struct
    {
        u32 Rm : 4;
        u32 : 1;
        u32 ShiftType : 2;
        u32 ShiftImm : 5;
    };
};

void ARM_LoadStore(ARM* cpu, const ARM_Instr instr_data)
{
    const union ARM_LoadStore_Decode instr = {.Raw = instr_data.Raw};

    // worth noting that unlike the thumb version this doesn't enforce word alignment for pc
    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;
    u32 offset;

    // calculate offset
    if (!instr.Register)
    {
        // immediate offset
        offset = instr.Imm12;
    }
    else
    {
        // scaled register offset
        offset = ARM_GetReg(instr.Rm);

        // perform shift
        switch(instr.ShiftType)
        {
        case 0: // lsl
        {
            offset <<= instr.ShiftImm;
            break;
        }
        case 1: // lsr
        {
            u32 shift = instr.ShiftImm;
            if (shift == 0) shift = 32;

            offset = ((u64)offset >> shift);
            break;
        }
        case 2: // asr
        {
            u32 shift = instr.ShiftImm;
            if (shift == 0) shift = 32;

            offset = ((s64)(s32)offset >> shift);
            break;
        }
        case 3: // ror/rrx
        {
            if (instr.ShiftImm) // ror
                offset = ROR32(offset, instr.ShiftImm);
            else // rrx
                offset = (cpu->CPSR.Carry << 31) | (offset >> 1);
            break;
        }
        }
    }

    // handle writeback modes.
    bool writeback = (instr.Writeback || (!instr.PreIndex));
    u32 wbaddr = addr;
    if (instr.Up) wbaddr += offset;
    else          wbaddr -= offset;

    if (instr.PreIndex) addr = wbaddr;

    ARM_StepPC(cpu, false);

    bool priv = ((instr.Writeback && (!instr.PreIndex)) ? false : cpu->Privileged);
    ARM_DataWidth size = instr.Byte ? ARMDataWidth_8 : ARMDataWidth_32;
    if (instr.Load) ARM_LDR(cpu, addr, instr.Rd, priv, instr.Rn, wbaddr, baserestore, writeback, size, false);
    else            ARM_STR(cpu, addr, instr.Rd, priv, instr.Rn, wbaddr, baserestore, writeback, size);
}

s8 A9ES_LoadStore_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry)
{
    const union ARM_LoadStore_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rn == reg) return len;
    if (instr.Register && (instr.Rm == reg)) return len;
    if (!instr.Load && (instr.Rd == reg)) return len_c-1;

    // dont test rn since its an input already
    if ((len > 1) && (!instr.Load || (instr.Rd != reg))) *retry = true;

    return 0;
}

union ARM_LoadStoreMisc_Decode
{
    u32 Raw;
    struct
    {
        u32 ImmLo : 4;
        u32 : 1;
        u32 OpcodeLo : 2;
        u32 : 1;
        u32 ImmHi : 4;
        u32 Rd : 4;
        u32 Rn : 4;
        u32 OpcodeHi : 1;
        u32 Writeback : 1;
        u32 Immediate : 1;
        u32 Up : 1;
        u32 PreIndex : 1;
    };
    struct
    {
        u32 Rm : 4;
    };
};

void ARM_LoadStoreMisc(ARM* cpu, const ARM_Instr instr_data)
{
    const union ARM_LoadStoreMisc_Decode instr = {.Raw = instr_data.Raw};

    u8 opcode = instr.OpcodeLo | instr.OpcodeHi << 2;

    // strd and ldrd with "unaligned" Rd raise UDF
    // TODO: Does this interlock? I think it might...?
    // TODO: how does this actually work on ARM7?
    if ((opcode == 0b011 || opcode == 0b010) && ((instr.Rd & 1) || (cpu->CPUID == ARM7ID)))
        return ARM_RaiseUDF;

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;
    u32 offset;
    if (instr.Immediate) offset = instr.ImmLo | instr.ImmHi << 4;
    else offset = ARM_GetReg(instr.Rm);


    // handle writeback modes.
    bool writeback = (instr.Writeback || (!instr.PreIndex));
    u32 wbaddr = addr;
    if (instr.Up) wbaddr += offset;
    else          wbaddr -= offset;

    if (instr.PreIndex) addr = wbaddr;

    ARM_StepPC(cpu, false);
    switch(opcode)
    {
    case 0b001: ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, instr.Rn, wbaddr, baserestore, writeback, ARMDataWidth_16); break; // STRH
    // NOTE: LDRD immediate offset flag is the same bit as the S bit for ARM LDM, and since LDRD is implemented by reusing some of LDM's logic on ARM9E-S
    // it's likely that the LDM hw logic simply checks bit 22 of the instruction's data when writing to pc to determine if the spsr should be restored, thus why this undocumented encoding behaves this way.
    // presumably user reg logic is handled differently, to prevent ldrd/strd from triggering it.
    case 0b010: ARM_LDM(cpu, addr, 0x3<<instr.Rd, wbaddr, baserestore, instr.Rn, writeback, ((instr.Rd == 14) && instr.Immediate)); break; // LDRD
    case 0b011: ARM_STM(cpu, addr, 0x3<<instr.Rd, wbaddr, baserestore, instr.Rn, writeback, false); break; // STRD
    case 0b101: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, instr.Rn, wbaddr, baserestore, writeback, ARMDataWidth_16, false); break; // LDRH
    case 0b110: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, instr.Rn, wbaddr, baserestore, writeback, ARMDataWidth_8, true); break; // LDRSB
    case 0b111: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, instr.Rn, wbaddr, baserestore, writeback, ARMDataWidth_16, true); break; // LDRSH
    default: CrashSpectacularly("ARM%i: INVALID LOAD/STORE MISC OPCODE: %"PRIu8" @ %08"PRIX32"\n", CPUIDtoCPUNum, opcode, cpu->PC);
    }
}

s8 A9ES_LoadStoreMisc_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry)
{
    const union ARM_LoadStoreMisc_Decode instr = {.Raw = instr_data.Raw};
    u8 opcode = instr.OpcodeLo | (instr.OpcodeHi << 2);

    if (reg == instr.Rn) return len;
    if (!instr.Immediate && (reg == instr.Rm)) return len; // reg offset
    if (((opcode == 0b001) || (opcode == 0b011)) && (reg == instr.Rd)) return len_c-1; // strh + strd

    // dont test rn since its an input already
    if (!(((opcode == 0b010) && (reg == (instr.Rd+1))) // ldrd 
       || ((opcode >= 0b101) && (reg == instr.Rd)))) // ldrh + ldrsb + ldrsh
        *retry = true;
    return 0;
}

union ARM_LoadStoreMultiple_Decode
{
    u32 Raw;
    struct
    {
        u32 RList : 16;
        u32 Rn : 4;
        bool Load : 1;
        bool Writeback : 1;
        bool S : 1;
        bool Up : 1;
        bool PreInc : 1;

    };
};

void ARM_LoadStoreMultiple(ARM* cpu, const ARM_Instr instr_data)
{
    union ARM_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;

    u8 nregs = stdc_count_ones((u16)instr.RList);
    u16 rlist = instr.RList;

    // TODO: empty RList timings
    if (!instr.RList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID) rlist = 0x8000; // idk why, it just is.
    }

    ARM_ExeCycles(1, 1);

    ARM_StepPC(cpu, false);

    u32 wbaddr;
    if (instr.Up) wbaddr = (addr + (nregs*4));
    else          wbaddr = (addr -= (nregs*4)); // "decrementing" (actually starts from 'end' address)
    if (instr.PreInc ^ (!instr.Up)) addr += 4;

    // TODO: this instruction does weird shit after exec with the banked variant, at least on arm7.
    // CHECKME: this instruction might do weird shit with writeback and banked regs.

    if (instr.Load) ARM_LDM(cpu, addr, rlist, wbaddr, baserestore, instr.Rn, instr.Writeback, instr.S);
    else            ARM_STM(cpu, addr, rlist, wbaddr, baserestore, instr.Rn, instr.Writeback, instr.S);
}

s8 ARM9_LoadStoreMultiple_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry [[maybe_unused]])
{
    const union ARM_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rn == reg) return len;
    if (!instr.Load && ((s8)stdc_trailing_zeros((u32)instr.RList) == reg)) return len_c-1;

    // ldm/stm probably cannot be 1 cycle?
    return 0;
}

union ARM_Swap_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 8;
        u32 Rd : 4;
        u32 Rn : 4;
        u32 : 2;
        bool Byte : 1;
    };
};

void ARM_Swap(ARM* cpu, const ARM_Instr instr_data)
{
    const union ARM_Swap_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn);

    ARM_ExeCycles(1, 1);

    ARM_StepPC(cpu, false);

    if (cpu->CPUID == ARM7ID)
    {
        ARM7TDMI* a7tdmi = ARM7Cast;

        u32 wrdata = ARM_GetReg(instr.Rm);
        if (instr.Byte)
        {
            wrdata &= 0xFF;
            wrdata |= (wrdata << 8) | (wrdata << 16) | (wrdata << 24);
        }

        A7TDMI_PostMem pass = {
            .WrData[1] = wrdata, // checkme: pc should be +12
            .Addr = addr,
            .RListOrig = 1<<instr.Rd,
            .NumFetch = 1,
            .NumFetchCompleted = 0,
            .Size = (instr.Byte ? ARMDataWidth_8 : ARMDataWidth_32),
            .Priv = cpu->Privileged,
            .DataCB = A7TDMIDataCB_SwapLoad,
        };
        a7tdmi->PostMem = pass;
        A7TDMI_DataRead(a7tdmi, cpu->Timestamp+DSClk33(1));
    }
    else
    {
        ARM946ES* a9es = ARM9Cast;
        // test rd interlocks; has to be done now so we can know if an instruction can occur in sync with the store. (this might not actually matter? it is more similar to hw so...)
        u8 illen = 1+(instr.Byte || (addr & 3));
        s8 ildelay;
        bool retry = false;
        if (instr.Rd == 15) ildelay = illen; // pc always interlocks
        else ildelay = A9ES_DecodeInterlocks(a9es, cpu->CPSR.Thumb, instr.Rd, illen, 2, &retry);

        if (!ildelay)
        {
            ildelay = A9ES_TestTwoCycleInterlocks(a9es);
            if (ildelay) retry = false;
        }

        u32 wrdata = ARM_GetReg(instr.Rm);
        if (instr.Byte)
        {
            wrdata &= 0xFF;
            wrdata |= (wrdata << 8) | (wrdata << 16) | (wrdata << 24);
        }

        A9ES_PostMem pass = {
            .WrData[1] = wrdata, // checkme: pc should be +12
            .Addr = addr,
            .RListOrig = 1<<instr.Rd,
            .DataAbort = false,
            .NumFetch = 1,
            .NumFetchCompleted = 0,
            .Size = (instr.Byte ? ARMDataWidth_8 : ARMDataWidth_32),
            .Priv = cpu->Privileged,
            .ILDelay = ildelay,
            .ILRetry = retry,
            .DataCB = A9ESDataCB_SwapLoad,
        };
        A9ES_DataGo(a9es, &pass);
    }
}

void A9ES_SWPLoad_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort) // store does not occur if load was aborted
        return A9ES_DataAbort(a9es);

    // schedule store

    // hacky way to keep store data in index 1 and read data in index 0
    pass->NumFetch = 2;
    pass->NumFetchCompleted = 1;
    pass->DataCB = A9ESDataCB_SwapStore;
    A9ES_DataGo(a9es, &a9es->PostMem);
    // cursed note: an itcm instr load can be run between the load and store of a swp
    if (!pass->ILDelay)
        A9ES_InstrGo(a9es, false);
}

void A9ES_SWPStore_Post(ARM946ES* a9es)
{
    A9ES_PostMem* pass = &a9es->PostMem;
    if (pass->DataAbort) // mission failed
        return A9ES_DataAbort(a9es);

    // load writeback occurs now
    u32 rdata = pass->RData[0];
    u8 rd = stdc_trailing_zeros(pass->RListOrig);
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

    A7TDMI_InstrRead(a7tdmi, a7tdmi->ARM.Timestamp+DSClk33(1));
}

s8 A9ES_Swap_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry [[maybe_unused]])
{
    const union ARM_Swap_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rn == reg) return len;
    // checkme: i dont think the store can interlock?

    // retry not possible
    return 0;
}
