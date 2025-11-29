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
            // TODO

            u32 mask;
            if (instr.Byte)
            {
                mask = u8_max << ((addr & 3) * 8);
            }
            else mask = u32_max;

            val = ARM7_BusRead(ARM7Cast, addr, mask, &seq);

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
            val = ((instr.Byte) ? ARM9_DataRead8(ARM9Cast, addr, &seq, &dabt)
                                : ARM9_DataRead32(ARM9Cast, addr, &seq, &dabt));

            // RORing the result takes an extra cycle
            // masking out bits also incurs the extra cycle, so it always applies to byte accesses.
            interlock = ((instr.Byte || (addr & 3)) ? 2 : 1);

            // TODO: data abort
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
        u32 mask;

        if (instr.Byte)
        {
            val = ROL32(val, (addr & 3) * 8);
            mask = ROL32(u8_max, ((addr & 3) * 8));
        }
        else mask = u32_max;

        if (cpu->CPUID == ARM7ID)
        {
            ARM7_BusWrite(ARM7Cast, addr, val, mask, false, &seq);
            cpu->CodeSeq = false;
        }
        else
        {

            ARM9_DataWrite(ARM9Cast, addr, val, mask, false, true, &seq, &dabt);

            // TODO: Data abort
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

    if ((opcode == 0b011 || opcode == 0b010) && instr.Rd & 1)
    {
        // strd and ldrd with "unaligned" Rd raise UDF
        // TODO: Does this interlock? I think it might...?
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

        u32 mask;
        val = ROL32(val, (addr & 2) * 8);
        mask = ROL32(u16_max, ((addr & 2) * 8));

        if (cpu->CPUID == ARM7ID)
        {
            ARM7_BusWrite(ARM7Cast, addr, val, mask, false, &seq);
            cpu->CodeSeq = false;
        }
        else
        {
            ARM9_DataWrite(ARM9Cast, addr, val, mask, false, false, &seq, &dabt);
            // TODO: Data abort
        }
        break;
    }
    case 0b101: // LDRH
    case 0b111: // LDRSH
    {
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            u32 mask = u16_max << ((addr & 2) * 8);

            // arm7 needs 1 cycle extra after the load.
            // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
            cpu->Timestamp += 1;

            // no idea why, but base writeback to r15 makes writing back the loaded value fail on arm7.
            // CHECKME: does this still incur the idle cycle?
            if (instr.Rn == 15 && (instr.Writeback || (!instr.PreIndex)))
                return;

            val = ARM7_BusRead(ARM7Cast, addr, mask, &seq);

            // rotate result right based on lsb of address.
            val = ROR32(val, (addr&3) * 8);

            // sign extension is weird on ARM7
            if (opcode == 0b111)
                val = (addr & 1) ? ((s32)(s8)val) : ((s32)(s16)val);

            cpu->CodeSeq = false;
        }
        else
        {
            val = ARM9_DataRead16(ARM9Cast, addr, &seq, &dabt);
            // TODO: data abort

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

            ARM_SetReg(instr.Rd, val, 1, 2);
        }
        break;
    }
    case 0b110: // LDRSB
    {
        u32 val;
        if (cpu->CPUID == ARM7ID)
        {
            u32 mask = u16_max << ((addr & 3) * 8);

            // no idea why, but base writeback to r15 makes writing back the loaded value fail on arm7.
            // CHECKME: does this still incur the idle cycle?
            if (instr.Rn == 15 && (instr.Writeback || (!instr.PreIndex)))
                return;

            val = ARM7_BusRead(ARM7Cast, addr, mask, &seq);

            // arm7 needs 1 cycle extra after the load.
            // presumably this is for the same reason that certain loads can have writeback stage interlocks on arm9.
            cpu->Timestamp += 1;

            // rotate result right based on lsb of address.
            val = ROR32(val, (addr&3) * 8);

            // CHECKME: i dont remember if this is weird in some way or not
            val = (s8)val;

            cpu->CodeSeq = false;
        }
        else
        {
            val = ARM9_DataRead8(ARM9Cast, addr, &seq, &dabt);
            // TODO: data abort

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

            ARM_SetReg(instr.Rd, val, 1, 2);
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
    if (instr.OpcodeHi == 0 && ((instr.OpcodeLo == 0b00) || (instr.OpcodeLo == 0b11)))
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
    if (instr.S) CrashSpectacularly("UNIMPLEMENTED LDM/STM!!!! %08lX @ %08lX\n", instr_data.Raw, cpu->PC);

    u32 addr = ARM_GetReg(instr.Rn);
    u32 baserestore = addr;

    unsigned nregs = stdc_count_ones((u16)instr.RList);

    // TODO: handle empty Rlist
    if (!instr.RList)
    {
        CrashSpectacularly("EMPTY RLIST POP!!!\n");
    }

    // arm9 timings are input as 0 since they will be added during the actual fetch
    ARM_ExeCycles(1, 0, 0);

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

    u16 rlist = instr.RList;

    bool seq = false;
    bool dabt = false;
    if (instr.Load)
    {
        while(rlist)
        {
            unsigned reg = stdc_trailing_zeros(rlist);

            // read
            u32 val;
            if (cpu->CPUID == ARM7ID)
            {
                val = ARM7_BusRead(ARM7Cast, addr, u32_max, &seq);

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

                ARM_SetReg(reg, val, 1, 1);
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
                ARM7_BusWrite(ARM7Cast, addr, val, u32_max, false, &seq);

                // base writeback after first access
                if (instr.Writeback && (reg == stdc_trailing_zeros((u16)instr.RList)))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            else
            {
                ARM9_DataWrite(ARM9Cast, addr, val, u32_max, false, false, &seq, &dabt);

                // base writeback before last access
                if (instr.Writeback && (reg == (15-stdc_leading_zeros((u16)instr.RList))))
                    ARM_SetReg(instr.Rn, wbaddr, 0, 0);
            }
            // increment address
            addr += 4;

            rlist &= (~1)<<reg;
        }
    }

    if (nregs == 1)
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
