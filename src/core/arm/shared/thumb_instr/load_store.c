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
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, true);

    u32 val = ARM_GetReg(rd);
    bool seq = false;
    bool dabt = false;
    if (cpu->CPUID == ARM7ID)
    {
        if (width == width32) ARM7_DataWrite32(ARM7Cast, addr, val, false, &seq);
        if (width == width16) ARM7_DataWrite16(ARM7Cast, addr, val, &seq);
        if (width == width8 ) ARM7_DataWrite8 (ARM7Cast, addr, val, false, &seq);
        cpu->CodeSeq = false;
    }
    else
    {
        timestamp oldts = ARM9Cast->MemTimestamp;
        if (width == width32) ARM9_DataWrite32(ARM9Cast, addr, val, false, true, &seq, &dabt);
        if (width == width16) ARM9_DataWrite16(ARM9Cast, addr, val, &seq, &dabt);
        if (width == width8 ) ARM9_DataWrite8 (ARM9Cast, addr, val, false, &seq, &dabt);
        ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);
    }

    if (dabt)
    {
        ARM9_DataAbort(ARM9Cast);
    }
}

void LDR(struct ARM* cpu, const u32 addr, const u8 rd, const int width, const bool signext)
{
    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, true);

    bool seq = false;
    bool dabt = false;
    u32 val;
    u32 interlock = 0;
    if (cpu->CPUID == ARM7ID)
    {
        if (width == width32) val = ARM7_DataRead32(ARM7Cast, addr, &seq);
        if (width == width16) val = ARM7_DataRead16(ARM7Cast, addr, &seq);
        if (width == width8 ) val = ARM7_DataRead8 (ARM7Cast, addr, &seq);

        // arm7 needs 1 cycle extra after the load.
        // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
        cpu->Timestamp += 1;
        cpu->CodeSeq = false;

        // rotate result right based on lsb of address.
        val = ROR32(val, (addr&3) * 8);

        // sign extension is weird on ARM7 for 16 bit wide loads
        if (signext)
            val = (((width == width8) || (addr & 1)) ? ((s32)(s8)val) : ((s32)(s16)val));
    }
    else
    {
        timestamp oldts = ARM9Cast->MemTimestamp;
        if (width == width32) val = ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt);
        if (width == width16) val = ARM9_DataRead16(ARM9Cast, addr, &seq, &dabt);
        if (width == width8 ) val = ARM9_DataRead8 (ARM9Cast, addr, &seq, &dabt);
        ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);
        // TODO: data abort

        // RORing the result takes an extra cycle
        // masking out bits also incurs the extra cycle, so it always applies to byte/halfword accesses.
        interlock = (((width != width32) || (addr & 3)) ? 2 : 1);

        // rotate result right based on lsb of address.
        // doesn't apply to 16 bit wide loads on arm9
        if (width != width16)
            val = ROR32(val, (addr&3) * 8);

        if (width == width8)
        {
            val &= 0xFF;
        }

        if (signext)
            val = ((width == width8) ? ((s32)(s8)val) : ((s32)(s16)val));
    }

    if (!dabt)
    {
        // loads can interwork on arm9 when the disable bit is clear.
        if ((rd == 15) && ARM_CanLoadInterwork)
        {
            ARM_SetThumb(cpu, val & 1);
        }

        ARM_SetReg(rd, val, interlock, interlock+1);
    }
    else
    {
        ARM9_DataAbort(ARM9Cast);
    }
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
    unsigned nregs = stdc_count_ones(instr.FullRList);

    unsigned truenregs = nregs;
    timestamp oldts;
    if (cpu->CPUID == ARM9ID) oldts = ARM9Cast->MemTimestamp;

    u16 rlist = instr.RList;

    // TODO: empty RList timings
    if (!instr.FullRList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID) rlist = 0x8000; // idk why, it just is.
    }

    if (instr.Link)
    {
        rlist |= 0x4000;
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, true);

    // push is encoded as a decrementing before variant
    u32 wbaddr = (addr -= nregs*4);

    bool seq = false;
    bool dabt = false;
    while(rlist)
    {
        unsigned reg = stdc_trailing_zeros(rlist);

        // write
        u32 val = ARM_GetReg(reg);
        if (cpu->CPUID == ARM7ID)
        {
            ARM7_DataWrite32(ARM7Cast, addr, val, false, &seq);
        }
        else
        {
            ARM9_DataWrite32(ARM9Cast, addr, val, false, false, &seq, &dabt);
        }
        // increment address
        addr += 4;

        rlist &= (~1)<<reg;
    }

    if (cpu->CPUID == ARM9ID)
        ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);

    if (nregs == 1 || !instr.FullRList)
    {
        // not an interlock but close enough
        if (cpu->CPUID == ARM9ID)
            ARM9_InterlockStall(ARM9Cast, 1);
    }

    // clean up arm7 timings
    if (cpu->CPUID == ARM7ID)
    {
        cpu->CodeSeq = false;
    }


    if (!dabt)
    {
        // note: should technically be done after first iteration for arm7, but sp can't be in rlist so we can cheat
        ARM_SetReg(13, wbaddr, 0, 0);
    }
    else
    {
        ARM9_DataAbort(ARM9Cast);
    }
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
    unsigned nregs = stdc_count_ones(instr.FullRList);

    u16 rlist = instr.RList;

    unsigned truenregs = nregs;
    timestamp oldts;
    if (cpu->CPUID == ARM9ID) oldts = ARM9Cast->MemTimestamp;

    // TODO: empty RList timings
    if (!instr.FullRList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID) rlist = 0x8000; // idk why, it just is.
    }

    if (instr.Link)
    {
        rlist |= 0x8000;
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, true);

    // pop is encoded as an incrementing after variant
    u32 wbaddr = addr + (nregs*4);

    bool seq = false;
    bool dabt = false;
    bool earlyfix = false;
    while(rlist)
    {
        unsigned reg = stdc_trailing_zeros(rlist);

        // read
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            val = ARM7_DataRead32(ARM7Cast, addr, &seq);
        }
        else
        {
            val = ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt);
        }

        if (!dabt)
        {
            // loads can interwork on arm9 when the disable bit is clear.
            if ((reg == 15) && ARM_CanLoadInterwork)
            {
                ARM_SetThumb(cpu, val & 1);
            }

            if ((cpu->CPUID == ARM9ID) && (reg == 15))
            {
                ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);
                earlyfix = true;
            }

            ARM_SetReg(reg, val, 1, 2);
        }
        // increment address
        addr += 4;

        rlist &= (~1)<<reg;
    }

    if (cpu->CPUID == ARM9ID && !earlyfix)
        ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);

    if (nregs == 1 || !instr.FullRList)
    {
        // not an interlock but close enough
        if (cpu->CPUID == ARM9ID)
            ARM9_InterlockStall(ARM9Cast, 1);
    }

    // clean up arm7 timings
    if (cpu->CPUID == ARM7ID)
    {
        cpu->Timestamp += 1;
        cpu->CodeSeq = false;
    }

    if (!dabt)
    {
        // note: should technically be done after first iteration for arm7, but sp can't be in rlist so we can cheat
        ARM_SetReg(13, wbaddr, 0, 0);
    }
    else
    {
        ARM9_DataAbort(ARM9Cast);
    }
}

s8 THUMB9_Pop_Interlocks(struct ARM946ES* ARM9, [[maybe_unused]] const struct ARM_Instr instr_data)
{
    s8 stall = 0;

    // ig
    ARM9_CheckInterlocks(ARM9, &stall, 13, 0, false);

    return stall;
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

void THUMB_LoadStoreMultiple(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;

    u16 rlist = instr.RList;
    unsigned nregs = stdc_count_ones((u8)instr.RList);

    unsigned truenregs = nregs;
    timestamp oldts;
    if (cpu->CPUID == ARM9ID) oldts = ARM9Cast->MemTimestamp;

    // TODO: empty RList timings
    if (!instr.RList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID) rlist = 0x8000; // idk why, it just is.
    }
    const u16 rlistinit = rlist;

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, true);

    u32 wbaddr = addr + (nregs*4);

    bool seq = false;
    bool dabt = false;
    bool earlyfix = false;
    if (instr.Load)
    {
        while(rlist)
        {
            unsigned reg = stdc_trailing_zeros(rlist);

            // read
            u32 val;
            if (cpu->CPUID == ARM7ID)
            {
                val = ARM7_DataRead32(ARM7Cast, addr, &seq);

                // base writeback after first access
                if (reg == stdc_trailing_zeros(rlistinit))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            else
            {
                val = ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt);

                // base writeback before last access
                if (reg == 15-stdc_leading_zeros(rlistinit))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }


            if (!dabt)
            {
                // loads can interwork on arm9 when the disable bit is clear.
                if ((reg == 15) && ARM_CanLoadInterwork)
                {
                    ARM_SetThumb(cpu, val & 1);
                }

                if ((cpu->CPUID == ARM9ID) && (reg == 15))
                {
                    ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);
                    earlyfix = true;
                }

                ARM_SetReg(reg, val, 1, 2);
            }

            // increment address
            addr += 4;

            rlist &= (~1)<<reg;
        }
    }
    else
    {
        while(rlist)
        {
            unsigned reg = stdc_trailing_zeros(rlist);

            // write
            u32 val = ARM_GetReg(reg);
            if (cpu->CPUID == ARM7ID)
            {
                ARM7_DataWrite32(ARM7Cast, addr, val, false, &seq);

                // base writeback after first access
                if (reg == stdc_trailing_zeros(rlistinit))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            else
            {
                ARM9_DataWrite32(ARM9Cast, addr, val, false, false, &seq, &dabt);

                // base writeback before last access
                if (reg == (15-stdc_leading_zeros(rlistinit)))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            // increment address
            addr += 4;

            rlist &= (~1)<<reg;
        }
    }

    if (cpu->CPUID == ARM9ID && !earlyfix)
        ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);

    if (nregs == 1 || !instr.RList)
    {
        // not an interlock but close enough
        if (cpu->CPUID == ARM9ID)
        {
            ARM9_InterlockStall(ARM9Cast, 1);
            // writeback seems to always occur after the first fetch
            ARM_SetReg(instr.Rn, wbaddr, 0, 0);
        }
    }

    // clean up arm7 timings
    if (cpu->CPUID == ARM7ID)
    {
        cpu->Timestamp += 1;
        cpu->CodeSeq = false;
    }

    if (dabt)
    {
        ARM_SetReg(instr.Rn, baserestore, 0, 0);
        ARM9_DataAbort(ARM9Cast);
    }
}

s8 THUMB9_LoadStoreMultiple_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    // ig
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);

    if (!instr.Load && instr.RList)
    {
        int reg = stdc_trailing_zeros((u8)instr.RList);
        ARM9_CheckInterlocks(ARM9, &stall, reg, 1, true);
    }

    return stall;
}
