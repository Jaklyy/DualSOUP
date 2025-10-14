#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"



union ARM_LoadStoreReg_Decode
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

void ARM_LoadReg(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_LoadStoreReg_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rn);
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
        switch (instr.ShiftType)
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

            offset = ((u64)offset >> instr.ShiftImm);
            break;
        }
        case 2: // asr
        {
            u32 shift = instr.ShiftImm;
            if (shift == 0) shift = 32;

            offset = ((s64)(s32)offset >> instr.ShiftImm);
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
        ARM_SetReg(instr.Rn, wbaddr, 0, 0);
    }

    // translation alts; forces user accesses
    bool oldpriv = cpu->Privileged;
    if (instr.Writeback && (!instr.PreIndex))
    {
        cpu->Privileged = false;
    }

    // insert load store actually here
    u32 cycles;
    u32 val;

    // finish up instruction


    if ((instr.Rd == 15) && ARM_CanLoadInterwork)
    {
        ARM_SetThumb(cpu, val & 1);
    }

    ARM_SetReg(instr.Rd, val, cycles, cycles+1);
}
