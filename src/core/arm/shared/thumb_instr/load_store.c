#include "core/utils.h"
#include "../arm.h"
#include "../inc.h"




union THUMB_LoadStoreReg_Decode
{
    u16 Raw;
    struct
    {
        u16 Rd : 3;
        u16 Rn : 3;
        u16 Rm : 3;
        u16 Opcode : 3;
    };
};

void THUMB_LoadStoreReg(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreReg_Decode instr = {.Raw = instr_data.Thumb};

    u32 addr = ARM_GetReg(instr.Rn) + ARM_GetReg(instr.Rm);

    ARM_StepPC(cpu, true);

    switch(instr.Opcode)
    {
    case 0: ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_32); break; // str
    case 1: ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_16); break; // strh
    case 2: ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_8); break; // strb
    case 3: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_8, true); break; // ldrsb
    case 4: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_32, false); break; // ldr
    case 5: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_16, false); break; // ldrh
    case 6: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_8, false); break; // ldrb
    case 7: ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_16, true); break; // ldrsh
    }
}

s8 T9ES_LoadStoreReg_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry)
{
    const union THUMB_LoadStoreReg_Decode instr = {.Raw = instr_data.Thumb};

    if (reg == instr.Rn) return len;
    if (reg == instr.Rm) return len;
    if ((instr.Opcode < 3) && (reg == instr.Rd)) return len_c-1; // stores

    if ((len > 1) && ((instr.Opcode < 3) || (reg != instr.Rd))) // store or not rd
        *retry = true;
    return 0;
}

union THUMB_LoadStoreImm_Decode
{
    u16 Raw;
    struct
    {
        u16 Rd : 3;
        u16 Rn : 3;
        u16 Imm5 : 5;
        bool Load : 1;
    };
};

void THUMB_LoadStoreWordImm(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Thumb};
    u32 addr = ARM_GetReg(instr.Rn) + (instr.Imm5 * 4);

    ARM_StepPC(cpu, true);

    if (instr.Load) ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_32, false);
    else            ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_32);
}

void THUMB_LoadStoreHalfwordImm(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Thumb};
    u32 addr = ARM_GetReg(instr.Rn) + (instr.Imm5 * 2);

    ARM_StepPC(cpu, true);

    if (instr.Load) ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_16, false);
    else            ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_16);
}

void THUMB_LoadStoreByteImm(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Thumb};
    u32 addr = ARM_GetReg(instr.Rn) + (instr.Imm5);

    ARM_StepPC(cpu, true);

    if (instr.Load) ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_8, false);
    else            ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_8);
}

s8 T9ES_LoadStoreImm_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Raw};
    if (reg == instr.Rn) return len;
    if (!instr.Load && (reg == instr.Rd)) return len_c-1;

    if ((len > 1) && (!instr.Load || (reg != instr.Rd)))
        *retry = true;
    return 0;
}

union THUMB_LoadStoreRel_Decode
{
    u16 Raw;
    struct
    {
        u16 Imm8 : 8;
        u16 Rd : 3;
        bool Load : 1;
    };
};

void THUMB_LoadPCRel(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreRel_Decode instr = {.Raw = instr_data.Raw};

    // instruction explictly forces pc to be word aligned.
    u32 addr = (ARM_GetReg(15) & ~3) + (instr.Imm8 * 4);

    ARM_StepPC(cpu, true);

    ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, ARMDataWidth_32, false, u8_max, 0, 0, false);
}

void THUMB_LoadStoreSPRel(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreRel_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(13) + (instr.Imm8 * 4);

    ARM_StepPC(cpu, true);

    if (instr.Load) ARM_LDR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_32, false);
    else            ARM_STR(cpu, addr, instr.Rd, cpu->Privileged, u8_max, 0, 0, false, ARMDataWidth_32);
}

s8 T9ES_LoadStoreSPRel_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry)
{
    const union THUMB_LoadStoreRel_Decode instr = {.Raw = instr_data.Raw};

    if (reg == 13) return len; // Note: probably not easy to trigger in practice
    if (!instr.Load && (reg == instr.Rd)) return len_c-1;

    if ((len > 1) && (!instr.Load || (reg != instr.Rd)))
        *retry = true;
    return 0;
}

union THUMB_PushPop_Decode
{
    u16 Raw;
    struct
    {
        u16 RList : 8;
        u16 Link : 1;
    };
    struct
    {
        u16 FullRList : 9;
    };
};

void THUMB_Push(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_PushPop_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(13);
    u32 baserestore = addr;
    u8 nregs = stdc_count_ones(instr.FullRList);

    u16 rlist = instr.RList;

    // TODO: empty RList timings
    if (!instr.FullRList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID)
            rlist = 0x8000; // idk why, it just is.
    }
    else if (instr.Link) rlist |= 0x4000;

    ARM_ExeCycles(1, 1);

    ARM_StepPC(cpu, true);

    // push is encoded as a decrementing before variant
    u32 wbaddr = (addr -= nregs*4);

    ARM_STM(cpu, addr, rlist, wbaddr, baserestore, 13, true, false);
}

typedef struct
{
    union
    {
        ARM946ES* ARM9;
        ARM7TDMI* ARM7;
    };
    u32 WBAddr;
    u16 RList;
    u8 Rb;
    bool First;
} ARMLDMCallback;

s8 T9ES_Push_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry [[maybe_unused]])
{
    const union THUMB_PushPop_Decode instr = {.Raw = instr_data.Raw};
    if (13 == reg) return len;
    if (((s8)stdc_trailing_zeros((u32)instr.RList | (instr.Link<<14)) == reg)) return len_c-1;

    // push probably cannot be 1 cycle?
    return 0;
}

void THUMB_Pop(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_PushPop_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(13);
    u32 baserestore = addr;
    u8 nregs = stdc_count_ones(instr.FullRList);

    u16 rlist = instr.RList;

    // TODO: empty RList timings
    if (!instr.FullRList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID)
            rlist = 0x8000; // idk why, it just is.
    }
    else if (instr.Link)
        rlist |= 0x8000;

    ARM_ExeCycles(1, 1);

    ARM_StepPC(cpu, true);

    // pop is encoded as an incrementing after variant
    u32 wbaddr = addr + (nregs*4);

    ARM_LDM(cpu, addr, rlist, wbaddr, baserestore, 13, true, false);
}

s8 T9ES_Pop_Interlocks(const ARM_Instr instr_data [[maybe_unused]], const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry [[maybe_unused]])
{
    if (13 == reg) return len;

    // pop probably cannot be 1 cycle?
    return 0;
}

union THUMB_LoadStoreMultiple_Decode
{
    u16 Raw;
    struct
    {
        u16 RList : 8;
        u16 Rn : 3;
        bool Load : 1;
    };
};

void THUMB_LoadStoreMultiple(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;

    u16 rlist = instr.RList;
    u8 nregs = stdc_count_ones((u8)instr.RList);

    // TODO: empty RList timings
    if (!instr.RList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID)
            rlist = 0x8000; // idk why, it just is.
    }

    ARM_ExeCycles(1, 1);

    ARM_StepPC(cpu, true);

    u32 wbaddr = addr + (nregs*4);

    bool ldmiawb = !((1<<instr.Rn) & rlist); // ldmia only does writeback if base is not in rlist
    if (instr.Load) ARM_LDM(cpu, addr, rlist, wbaddr, baserestore, instr.Rn, ldmiawb, false);
    else            ARM_STM(cpu, addr, rlist, wbaddr, baserestore, instr.Rn, true, false);
}

s8 T9ES_LoadStoreMultiple_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry [[maybe_unused]])
{
    const union THUMB_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rn == reg) return len;
    if (!instr.Load && ((s8)stdc_trailing_zeros((u32)instr.RList) == reg)) return len_c-1;

    // ldm/stm probably cannot be 1 cycle?
    return 0;
}
