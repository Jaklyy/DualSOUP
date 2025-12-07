#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"



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

void ARM_LoadStore(struct ARM* cpu, const struct ARM_Instr instr_data)
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
            {
                offset = ROR32(offset, instr.ShiftImm);
            }
            else // rrx
            {
                offset = (cpu->CPSR.Carry << 31) | (offset >> 1);
            }
            break;
        }
        }
    }

    // handle writeback modes.
    u32 wbaddr = addr;
    if (instr.Up)
    {
        wbaddr += offset;
    }
    else
    {
        wbaddr -= offset;
    }

    if (instr.PreIndex)
    {
        addr = wbaddr;
    }

    if (instr_data.Raw == 0xE59FD080) printf("confirmed\n");

    // actually writeback
    if (instr.Writeback || (!instr.PreIndex))
    {
        if (instr.Rn == 15)
        {
            // base writeback to PC
            if (cpu->CPUID == ARM7ID)
            {
                // it's always fun when an "unpredictable" instruction encoding does something that leaves you genuinely flabbergasted.
                // the actual load is +8
                // writeback value is +12
                // the actual load is also not properly written back afterwards for some reason
                // this is not the correct way to emulate this. but the correct way to do this is probably stupid.
                // dont ask me why this only applies to Loads....
                if (instr.Load)
                    wbaddr += 4;
            }
            else if (cpu->CPUID == ARM9ID)
            {
                // at least it's a sensible outcome...?
                // arm9 cannot perform base writeback to the program counter.
                goto skipwriteback;
            }
        }

        ARM_SetReg(instr.Rn, wbaddr, 0, 0);
    }
    skipwriteback:

    // translation alts; forces user accesses
    bool oldpriv = cpu->Privileged;
    if (instr.Writeback && (!instr.PreIndex))
    {
        cpu->Privileged = false;
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, false);

    bool seq = false;
    bool dabt = false;
    if (instr.Load)
    {
        // Load
        u32 val;
        u32 interlock = 0;
        if (cpu->CPUID == ARM7ID)
        {
            val = ((instr.Byte) ? ARM7_DataRead8(ARM7Cast, addr, &seq)
                                : ARM7_DataRead32(ARM7Cast, addr, &seq));

            // arm7 needs 1 cycle extra after the load.
            // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
            cpu->Timestamp+=1;
            cpu->CodeSeq = false;

            // no idea why, but base writeback to r15 makes writing back the loaded value fail on arm7.
            // CHECKME: does this still incur the idle cycle?
            if (instr.Rn == 15 && (instr.Writeback || (!instr.PreIndex)))
            {
                cpu->Privileged = oldpriv;
                return;
            }
        }
        else
        {
            timestamp oldts = ARM9Cast->MemTimestamp;
            val = ((instr.Byte) ? ARM9_DataRead8(ARM9Cast, addr, &seq, &dabt)
                                : ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt));
            ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);
            // RORing the result takes an extra cycle
            // masking out bits also incurs the extra cycle, so it always applies to byte accesses.
            interlock = ((instr.Byte || (addr & 3)) ? 2 : 1);
        }

        if (!dabt)
        {
            // rotate result right based on lsb of address.
            val = ROR32(val, (addr&3) * 8);

            if (instr.Byte)
            {
                val &= 0xFF;
            }

            // loads can interwork on arm9 when the disable bit is clear.
            if ((instr.Rd == 15) && ARM_CanLoadInterwork)
            {
                ARM_SetThumb(cpu, val & 1);
            }

            ARM_SetReg(instr.Rd, val, interlock, interlock+1);
        }
    }
    else
    {
        // Store
        u32 val = ARM_GetReg(instr.Rd);

        if (cpu->CPUID == ARM7ID)
        {
            ((instr.Byte) ? ARM7_DataWrite8(ARM7Cast, addr, val, false, &seq)
                          : ARM7_DataWrite32(ARM7Cast, addr, val, false, &seq));
            cpu->CodeSeq = false;
        }
        else
        {
            timestamp oldts = ARM9Cast->MemTimestamp;
            ((instr.Byte) ? ARM9_DataWrite8(ARM9Cast, addr, val, false, &seq, &dabt)
                          : ARM9_DataWrite32(ARM9Cast, addr, val, false, true, &seq, &dabt));
            ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);
        }
    }

    if (dabt)
    {
        ARM_SetReg(instr.Rn, baserestore, 0, 0);
        ARM9_DataAbort(ARM9Cast);
    }

    // oh yeah restore this too.
    cpu->Privileged = oldpriv;
}

s8 ARM9_LoadStore_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_LoadStore_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    // scaled reg
    if (instr.Register)
    {
        // checkme: delay & port
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    }
    // str
    if (!instr.Load)
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);
    }
    return stall;
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

void ARM_LoadStoreMisc(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_LoadStoreMisc_Decode instr = {.Raw = instr_data.Raw};

    u32 opcode = instr.OpcodeLo | instr.OpcodeHi << 2;

    if ((opcode == 0b011 || opcode == 0b010) && ((instr.Rd & 1) || (cpu->CPUID == ARM7ID)))
    {
        // strd and ldrd with "unaligned" Rd raise UDF
        // TODO: Does this interlock? I think it might...?

        // TODO: how does this actually work on ARM7?
        return ARM_RaiseUDF;
    }

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;
    u32 offset;
    if (instr.Immediate)
    {
        offset = instr.ImmLo | instr.ImmHi << 4;
    }
    else
    {
        offset = ARM_GetReg(instr.Rm);
    }

    // handle writeback modes.
    u32 wbaddr = addr;
    if (instr.Up)
    {
        wbaddr += offset;
    }
    else
    {
        wbaddr -= offset;
    }

    if (instr.PreIndex)
    {
        addr = wbaddr;
    }

    // actually writeback; NOTE: ldrd/strd do writeback elsewhere on arm9
    if ((instr.Writeback || (!instr.PreIndex)) && (opcode != 0b010) && (opcode != 0b011))
    {
        if (instr.Rn == 15)
        {
            // base writeback to PC
            if (cpu->CPUID == ARM7ID)
            {
                // it's always fun when an "unpredictable" instruction encoding does something that leaves you genuinely flabbergasted.
                // the actual load is +8
                // writeback value is +12
                // the actual load is also not properly written back afterwards for some reason
                // this is not the correct way to emulate this. but the correct way to do this is probably stupid.
                // dont ask me why this only applies to Loads....
                if (instr.OpcodeHi)
                    wbaddr += 4;
            }
            else if (cpu->CPUID == ARM9ID)
            {
                // at least it's a sensible outcome...?
                // arm9 cannot perform base writeback to the program counter.
                goto skipwriteback;
            }
        }

        ARM_SetReg(instr.Rn, wbaddr, 0, 0);
    }
    skipwriteback:

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, false);

    bool seq = false;
    bool dabt = false;
    switch(opcode)
    {
    case 0b001: // STRH
    {
        // Store
        u32 val = ARM_GetReg(instr.Rd);

        if (cpu->CPUID == ARM7ID)
        {
            ARM7_DataWrite16(ARM7Cast, addr, val, &seq);
            cpu->CodeSeq = false;
        }
        else
        {
            timestamp oldts = ARM9Cast->MemTimestamp;
            ARM9_DataWrite16(ARM9Cast, addr, val, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);
        }
        break;
    }
    case 0b010: // LDRD
    case 0b011: // STRD
    {
        if (cpu->CPUID == ARM7ID) CrashSpectacularly("ARM7 LDRD/STRD!!!");
        // these are actually both implemented as ldm/stm on arm9!
        timestamp oldts;
        if (opcode == 0b010) // LOAD
        {
            oldts = ARM9Cast->MemTimestamp;
            u32 val = ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 2, ARM9Cast->MemTimestamp - oldts);
            if (!dabt)
            {
                ARM_SetReg(instr.Rd, val, 1, 2);
            }
        }
        else
        {
            u32 val = ARM_GetReg(instr.Rd);
            oldts = ARM9Cast->MemTimestamp;
            ARM9_DataWrite(ARM9Cast, addr, val, u32_max, false, false, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 2, ARM9Cast->MemTimestamp - oldts);
        }

        if (opcode == 0b010) // LOAD
        {
            u32 val = ARM9_DataRead32(ARM9Cast, addr+4, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 2, ARM9Cast->MemTimestamp - oldts);

            // actually writeback now
            if (instr.Writeback || (!instr.PreIndex))
                ARM_SetReg(instr.Rn, wbaddr, 0, 0);

            if (!dabt)
            {
                if (((instr.Rd+1) == 15) && ARM_CanLoadInterwork)
                {
                    ARM_SetThumb(cpu, val & 1);
                }

                // bit 22... still does this for some reason.
                if (instr.Immediate && ((instr.Rd+1) == 15))
                {
                    ARM_RestoreSPSR;
                }

                ARM_SetReg(instr.Rd+1, val, 1, 2);
            }
        }
        else
        {
            u32 val = ARM_GetReg(instr.Rd+1);

            // actually writeback now
            if (instr.Writeback || (!instr.PreIndex))
                ARM_SetReg(instr.Rn, wbaddr, 0, 0);

            ARM9_DataWrite(ARM9Cast, addr+4, val, u32_max, false, false, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 2, ARM9Cast->MemTimestamp - oldts);
        }
        break;
    }
    case 0b101: // LDRH
    case 0b111: // LDRSH
    {
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            // no idea why, but base writeback to r15 makes writing back the loaded value fail on arm7.
            // CHECKME: does this still incur the idle cycle?
            if (instr.Rn == 15 && (instr.Writeback || (!instr.PreIndex)))
                return;

            val = ARM7_DataRead16(ARM7Cast, addr, &seq);

            // arm7 needs 1 cycle extra after the load.
            // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
            cpu->Timestamp += 1;
            cpu->CodeSeq = false;

            // rotate result right based on lsb of address.
            val = ROR32(val, (addr&3) * 8);

            // sign extension is weird on ARM7
            if (opcode == 0b111)
                val = (addr & 1) ? ((s32)(s8)val) : ((s32)(s16)val);
        }
        else
        {
            timestamp oldts = ARM9Cast->MemTimestamp;
            val = ARM9_DataRead16(ARM9Cast, addr, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);

            // Note: ARM9 doesn't ROR weirdly for LDRH

            // sign extend
            if (opcode == 0b111)
                val = ((s32)(s16)val);
        }

        if (!dabt)
        {
            // loads can interwork on arm9 when the disable bit is clear.
            if ((instr.Rd == 15) && ARM_CanLoadInterwork)
            {
                ARM_SetThumb(cpu, val & 1);
            }

            ARM_SetReg(instr.Rd, val, 2, 3);
        }
        break;
    }
    case 0b110: // LDRSB
    {
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            // no idea why, but base writeback to r15 makes writing back the loaded value fail on arm7.
            // CHECKME: does this still incur the idle cycle?
            if (instr.Rn == 15 && (instr.Writeback || (!instr.PreIndex)))
                return;

            val = ARM7_DataRead8(ARM7Cast, addr, &seq);

            // arm7 needs 1 cycle extra after the load.
            // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
            cpu->Timestamp += 1;
            cpu->CodeSeq = false;

            // rotate result right based on lsb of address.
            val = ROR32(val, (addr&3) * 8);

            // CHECKME: i dont remember if this is weird in some way or not
            val = (s8)val;
        }
        else
        {
            timestamp oldts = ARM9Cast->MemTimestamp;
            val = ARM9_DataRead8(ARM9Cast, addr, &seq, &dabt);
            ARM9_FixupLoadStore(ARM9Cast, 1, ARM9Cast->MemTimestamp - oldts);

            // rotate result right based on lsb of address.
            val = ROR32(val, (addr&3) * 8);

            // sign extend
            val = (s8)val;
        }

        if (!dabt)
        {
            // loads can interwork on arm9 when the disable bit is clear.
            if ((instr.Rd == 15) && ARM_CanLoadInterwork)
            {
                ARM_SetThumb(cpu, val & 1);
            }

            ARM_SetReg(instr.Rd, val, 2, 3);
        }
        break;
    }
    default: CrashSpectacularly("INVALID LOAD/STORE MISC OPCODE!!!!!!!\n"); break;
    }

    if (dabt)
    {
        ARM_SetReg(instr.Rn, baserestore, 0, 0);
        ARM9_DataAbort(ARM9Cast);
    }
}

s8 ARM9_LoadStoreMisc_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_LoadStoreMisc_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    // reg offset
    if (!instr.Immediate)
    {
        // checkme: delay & port
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    }
    // str
    if (instr.OpcodeHi == 0 && ((instr.OpcodeLo == 0b01) || (instr.OpcodeLo == 0b11)))
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);
    }
    return stall;
}

// we need this here to handle ldm/stm jank
void ARM9_InterlockStall(struct ARM946ES* ARM9, const s8 stall);

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

void ARM_LoadStoreMultiple(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    union ARM_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};

    bool preinc = instr.PreInc ^ (!instr.Up);

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;

    unsigned nregs = stdc_count_ones((u16)instr.RList);
    u16 rlist = instr.RList;

    unsigned truenregs = nregs;
    timestamp oldts = 0;
    if (cpu->CPUID == ARM9ID) oldts = ARM9Cast->MemTimestamp;

    // TODO: empty RList timings
    if (!instr.RList)
    {
        nregs = 16;
        if (cpu->CPUID == ARM7ID) rlist = 0x8000; // idk why, it just is.
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, false);

    u32 wbaddr;

    if (instr.Up)
    {
        wbaddr = (addr + (nregs*4));
    }
    else
    {
        wbaddr = (addr -= (nregs*4));
    }

    if (preinc) addr += 4;

    // TODO: this instruction does weird shit after exec with the banked variant, at least on arm7.
    // CHECKME: this instruction might do weird shit with writeback and banked regs.
    u8 oldmode = cpu->CPSR.Mode;

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
                if (instr.Writeback && (reg == stdc_trailing_zeros((u16)instr.RList)))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            else
            {
                val = ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt);

                // base writeback before last access
                if (instr.Writeback && (reg == (15-stdc_leading_zeros((u16)instr.RList))))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }

            if (!dabt)
            {
                // loads can interwork on arm9 when the disable bit is clear.
                if ((reg == 15) && ARM_CanLoadInterwork)
                {
                    ARM_SetThumb(cpu, val & 1);
                }

                if (instr.S && reg == 15)
                {
                    ARM_RestoreSPSR;
                }

                if (instr.S && !(instr.RList >> 15)) // dumb way to do this
                    ARM_SetMode(cpu, ARMMode_USR);

                if ((cpu->CPUID == ARM9ID) && (reg == 15))
                {
                    ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);
                    earlyfix = true;
                }

                ARM_SetReg(reg, val, 1, 2);

                if (instr.S && !(instr.RList >> 15)) // dumb way to do this
                    ARM_SetMode(cpu, oldmode);
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

            if (instr.S && !(instr.RList >> 15)) // dumb way to do this
                ARM_SetMode(cpu, ARMMode_USR);

            u32 val = ARM_GetReg(reg);

            if (instr.S && !(instr.RList >> 15)) // dumb way to do this
                ARM_SetMode(cpu, oldmode);

            if (cpu->CPUID == ARM7ID)
            {
                ARM7_DataWrite32(ARM7Cast, addr, val, false, &seq);

                // base writeback after first access
                if (instr.Writeback && (reg == stdc_trailing_zeros((u16)instr.RList)))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            else
            {
                ARM9_DataWrite32(ARM9Cast, addr, val, false, false, &seq, &dabt);

                // base writeback before last access
                if (instr.Writeback && (reg == (15-stdc_leading_zeros((u16)instr.RList))))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            // increment address
            addr += 4;

            rlist &= (~1)<<reg;
        }
    }

    if ((cpu->CPUID == ARM9ID) && !earlyfix)
        ARM9_FixupLoadStore(ARM9Cast, truenregs, ARM9Cast->MemTimestamp - oldts);

    if (nregs == 1 || !instr.RList) // empty r-list behavior is a guess.
    {
        if (cpu->CPUID == ARM9ID)
        {
            // CHECKME: does this occur if it data aborted?
            // not an interlock but close enough
            ARM9_InterlockStall(ARM9Cast, 1);
            // writeback seems to always occur after the first fetch
            if (instr.Writeback)
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

s8 ARM9_LoadStoreMultiple_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_LoadStoreMultiple_Decode instr = {.Raw = instr_data.Raw};
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

void ARM_Swap(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_Swap_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn);
    u32 store = ARM_GetReg(instr.Rm);

    bool dabt = false;
    bool seq = false;
    u32 load;
    int interlock = 0;

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 1, 0);

    ARM_StepPC(cpu, false);
    timestamp oldts;
    if (cpu->CPUID == ARM9ID) oldts = ARM9Cast->MemTimestamp;

    if (cpu->CPUID == ARM7ID)
    {
        load = ((instr.Byte) ? ARM7_DataRead8(ARM7Cast, addr, &seq)
                            : ARM7_DataRead32(ARM7Cast, addr, &seq));
        cpu->CodeSeq = false;
    }
    else
    {
        load = ((instr.Byte) ? ARM9_DataRead8(ARM9Cast, addr, &seq, &dabt)
                            : ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt));

        // RORing the result takes an extra cycle
        // masking out bits also incurs the extra cycle, so it always applies to byte accesses.
        interlock = ((instr.Byte || (addr & 3)) ? 2 : 1);
    }
    if (cpu->CPUID == ARM9ID) ARM9_FixupLoadStore(ARM9Cast, 2, ARM9Cast->MemTimestamp - oldts);

    if (!dabt)
    {
        if (cpu->CPUID == ARM7ID)
        {
            ((instr.Byte) ? ARM7_DataWrite8(ARM7Cast, addr, store, false, &seq)
                          : ARM7_DataWrite32(ARM7Cast, addr, store, false, &seq));
            cpu->CodeSeq = false;

            // load writeback occurs here.
            cpu->Timestamp += 1;
        }
        else
        {
            ((instr.Byte) ? ARM9_DataWrite8(ARM9Cast, addr, store, false, &seq, &dabt)
                          : ARM9_DataWrite32(ARM9Cast, addr, store, false, true, &seq, &dabt));
        }

        // rotate result right based on lsb of address.
        load = ROR32(load, (addr&3) * 8);

        if (instr.Byte)
        {
            load &= 0xFF;
        }

        // loads can interwork on arm9 when the disable bit is clear.
        if ((instr.Rd == 15) && ARM_CanLoadInterwork)
        {
            ARM_SetThumb(cpu, load & 1);
        }
        ARM_SetReg(instr.Rd, load, interlock, interlock+1);
    }
}

s8 ARM9_Swap_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_Swap_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);

    // checkme: i dont think the store can interlock?
    return stall;
}
