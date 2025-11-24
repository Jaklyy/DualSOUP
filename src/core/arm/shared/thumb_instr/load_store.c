#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"
#include <stdbit.h>




enum
{
    width32,
    width16,
    width8,
};

void STR(struct ARM* cpu, const u32 addr, const u8 rd, const int width)
{
    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 0, 0);

    ARM_StepPC(cpu, true);

    u32 val = ARM_GetReg(rd);
    bool seq = false;
    bool dabt = false;
    if (cpu->CPUID == ARM7ID)
    {
        // TODO
        cpu->CodeSeq = false;
    }
    else
    {
        u32 mask;
        if (width == width32) { mask = 0xFFFFFFFF; }
        if (width == width16) { mask = ROR32(0xFFFF, (addr & 2) * 8); val = ROR32(val, (addr & 2) * 8); }
        if (width == width8 ) { mask = ROR32(0xFF  , (addr & 3) * 8); val = ROR32(val, (addr & 3) * 8); }

        ARM9_DataWrite((struct ARM946ES*)cpu, addr, val, mask, false, &seq, &dabt);
        // TODO: data abort
    }

    ARM_SetReg(rd, val, 0, 0);
}

void LDR(struct ARM* cpu, const u32 addr, const u8 rd, const int width, const bool signext)
{
    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 0, 0);

    ARM_StepPC(cpu, true);

    bool seq = false;
    bool dabt = false;
    u32 val;
    u32 interlock = 0;
    if (cpu->CPUID == ARM7ID)
    {
        // TODO
        cpu->CodeSeq = false;
        val = 0;

        // arm7 needs 1 cycle extra after the load.
        // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
        ARM_ExeCycles(1, 1, 1);

        // rotate result right based on lsb of address.
        val = ROR32(val, (addr&3) * 8);

            // sign extension is weird on ARM7 for 16 bit wide loads
            if (signext)
                val = (((width == width8) || (addr & 1)) ? ((s32)(s8)val) : ((s32)(s16)val));
    }
    else
    {
        if (width == width32) val = ARM9_DataRead32((struct ARM946ES*)cpu, addr, &seq, &dabt);
        if (width == width16) val = ARM9_DataRead16((struct ARM946ES*)cpu, addr, &seq, &dabt);
        if (width == width8 ) val = ARM9_DataRead8 ((struct ARM946ES*)cpu, addr, &seq, &dabt);
        // TODO: data abort

        // RORing the result takes an extra cycle
        // masking out bits also incurs the extra cycle, so it always applies to byte/halfword accesses.
        interlock = (((width != width32) || (addr & 3)) ? 2 : 1);

        // rotate result right based on lsb of address.
        // doesn't apply to 16 bit wide loads on arm9
        if (width != width16)
            val = ROR32(val, (addr&3) * 8);

        if (signext)
            val = ((width == width8) ? ((s32)(s8)val) : ((s32)(s16)val));
    }

    ARM_SetReg(rd, val, interlock, interlock+1);
}

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

void THUMB_LoadStoreReg(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreReg_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn) + ARM_GetReg(instr.Rm);

    switch(instr.Opcode)
    {
    case 0: // str
        STR(cpu, addr, instr.Rd, width32); break;
    case 1: // strh
        STR(cpu, addr, instr.Rd, width16); break;
    case 2: // strb
        STR(cpu, addr, instr.Rd, width8); break;
    case 3: // ldrsb
        LDR(cpu, addr, instr.Rd, width8, true); break;
    case 4: // ldr
        LDR(cpu, addr, instr.Rd, width32, false); break;
    case 5: // ldrh
        LDR(cpu, addr, instr.Rd, width16, false); break;
    case 6: // ldrb
        LDR(cpu, addr, instr.Rd, width8, false); break;
    case 7: // ldrsh
        LDR(cpu, addr, instr.Rd, width16, true); break;
    }
}

s8 THUMB9_LoadStoreReg_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreReg_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    if (instr.Opcode < 3) // stores
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);

    return stall;
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

void THUMB_LoadStoreWordImm(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Raw};
    u32 addr = ARM_GetReg(instr.Rn) + (instr.Imm5 * 4);

    if (instr.Load) LDR(cpu, addr, instr.Rd, width32, false);
    else STR(cpu, addr, instr.Rd, width32);
}

void THUMB_LoadStoreHalfwordImm(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Raw};
    u32 addr = ARM_GetReg(instr.Rn) + (instr.Imm5 * 2);

    if (instr.Load) LDR(cpu, addr, instr.Rd, width16, false);
    else STR(cpu, addr, instr.Rd, width16);
}

void THUMB_LoadStoreByteImm(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Raw};
    u32 addr = ARM_GetReg(instr.Rn) + (instr.Imm5);

    if (instr.Load) LDR(cpu, addr, instr.Rd, width8, false);
    else STR(cpu, addr, instr.Rd, width8);
}

s8 THUMB9_LoadStoreImm_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreImm_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);

    if (!instr.Load)
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);

    return stall;
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

void THUMB_LoadPCRel(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreRel_Decode instr = {.Raw = instr_data.Raw};

    // instruction is unique in that it forces pc to be aligned.
    u32 addr = (ARM_GetReg(15) & ~3) + (instr.Imm8 * 4);

    // TODO: check for pc relative store...?
    LDR(cpu, addr, instr.Rd, width32, false);
}

void THUMB_LoadStoreSPRel(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreRel_Decode instr = {.Raw = instr_data.Raw};

    // instruction is unique in that it forces pc to be aligned.
    u32 addr = ARM_GetReg(13) + (instr.Imm8 * 4);

    if (instr.Load) LDR(cpu, addr, instr.Rd, width32, false);
    else STR(cpu, addr, instr.Rd, width32);
}

s8 THUMB9_LoadStoreSPRel_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreRel_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, 13, 0, false);

    if (!instr.Load)
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);

    return stall;
}

// we need this here to handle ldm/stm jank
void ARM9_InterlockStall(struct ARM946ES* ARM9, const s8 stall);

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

void THUMB_Push(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_PushPop_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(13);
    int nregs = stdc_count_ones(instr.FullRList);

    // TODO: handle empty Rlist
    if (nregs == 0)
    {
        CrashSpectacularly("EMPTY RLIST PUSH!!!\n");
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 0, 0);

    ARM_StepPC(cpu, true);

    u8 rlist = instr.RList;

    // push is encoded as a decrementing before variant
    u32 wbaddr = (addr -= nregs*4);

    bool seq = false;
    bool dabt = false;

    while(rlist)
    {
        int reg = stdc_trailing_zeros(rlist);

        // write
        if (cpu->CPUID == ARM7ID)
        {
            // TODO
        }
        else
        {
            ARM9_DataWrite((struct ARM946ES*)cpu, addr, ARM_GetReg(reg), 0xFFFFFFFF, false, &seq, &dabt);
        }
        // increment address
        addr += 4;

        rlist &= (~1)<<reg;
    }

    // push has a special encoding for pushing the link register
    if (instr.Link)
    {
        // write
        if (cpu->CPUID == ARM7ID)
        {
            // TODO
        }
        else
        {
            ARM9_DataWrite((struct ARM946ES*)cpu, addr, ARM_GetReg(14), 0xFFFFFFFF, false, &seq, &dabt);
        }
    }

    if (nregs == 1)
    {
        // not an interlock but close enough
        if (cpu->CPUID == ARM9ID)
            ARM9_InterlockStall((struct ARM946ES*)cpu, 1);
    }

    // note: should technically be done after first iteration for arm7, but sp can't be in rlist so we can cheat
    ARM_SetReg(13, wbaddr, 0, 0);
}

s8 THUMB9_Push_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_PushPop_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    // ig
    ARM9_CheckInterlocks(ARM9, &stall, 13, 0, false);

    if (instr.RList)
    {
        int reg = stdc_trailing_zeros((u8)instr.RList);
        ARM9_CheckInterlocks(ARM9, &stall, reg, 1, true);
    }
    else if (instr.Link)
    {
        // ig??
        ARM9_CheckInterlocks(ARM9, &stall, 14, 1, true);
    }

    return stall;
}

void THUMB_Pop(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_PushPop_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(13);
    int nregs = stdc_count_ones(instr.FullRList);

    // TODO: handle empty Rlist
    if (!instr.FullRList)
    {
        CrashSpectacularly("EMPTY RLIST POP!!!\n");
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 0, 0);

    ARM_StepPC(cpu, true);

    u8 rlist = instr.RList;

    // pop is encoded as an incrementing after variant
    u32 wbaddr = addr;

    bool seq = false;
    bool dabt = false;

    while(rlist)
    {
        int reg = stdc_trailing_zeros(rlist);

        // read
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            val = 0;
            // TODO
        }
        else
        {
            val = ARM9_DataRead32((struct ARM946ES*)cpu, addr, &seq, &dabt);
        }
        ARM_SetReg(reg, val, 1, 1);

        // increment address
        addr += 4;

        rlist &= (~1)<<reg;
    }

    // push has a special encoding for pushing the link register
    if (instr.Link)
    {
        // read
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            val = 0;
            // TODO
        }
        else
        {
            val = ARM9_DataRead32((struct ARM946ES*)cpu, addr, &seq, &dabt);
        }
        ARM_SetReg(15, val, 1, 1);
    }
    else if (nregs == 1)
    {
        // not an interlock but close enough
        if (cpu->CPUID == ARM9ID)
            ARM9_InterlockStall((struct ARM946ES*)cpu, 1);
    }

    // note: should technically be done after first iteration for arm7, but sp can't be in rlist so we can cheat
    ARM_SetReg(13, wbaddr, 0, 0);
}

s8 THUMB9_Pop_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    s8 stall = 0;

    // ig
    ARM9_CheckInterlocks(ARM9, &stall, 13, 0, false);

    return stall;
}