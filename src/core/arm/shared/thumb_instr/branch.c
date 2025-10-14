#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"




union THUMB_BranchCond_Decode
{
    u16 Raw;
    struct
    {
        s16 ImmS8 : 8;
        u16 Condition : 4;
    };
};

void THUMB_BranchCond(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_BranchCond_Decode instr = {.Raw = instr_data.Raw};

    // these condition codes are handled specially
    if (instr.Condition == ARMCond_NV)
    {
        return ARM_RaiseSWI;
    }
    else if (instr.Condition == ARMCond_AL)
    {
        return ARM_RaiseUDF;
    }

    ARM_ExeCycles(1, 1, 1);

    if (ARM_ConditionLookup(instr.Condition, cpu->CPSR.Flags))
    {
        u32 addr = ARM_GetReg(15);
        addr += (s32)instr.ImmS8 * 2;

        // dont bother stepping pc since we update it anyway.
        ARM_SetReg(15, addr, 0, 0);
    }
    else
    {
        ARM_StepPC(cpu, true);
    }
}

union THUMB_Branch_Decode
{
    u16 Raw;
    struct
    {
        s16 ImmS11 : 11;
        u16 Opcode : 2; // used 
    };
};

void THUMB_Branch(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_Branch_Decode instr = {.Raw = instr_data.Raw};

    // undefined instruction.
    if ((instr.Opcode == 0b01) && (instr.ImmS11 & 0x1))
    {
        return ARM_RaiseUDF;
    }

    ARM_ExeCycles(1, 1, 1);

    u32 pc = ARM_GetReg(15);
    s32 signedimm = instr.ImmS11 << ((instr.Opcode == 2) ? 12 : 1);
    u32 addr = ((instr.Opcode & 0b01) ? pc : ARM_GetReg(14));

    // CHECKME: this might somehow be faster to always do?
    if (instr.Opcode == 0b10)
    {
        ARM_StepPC(cpu, true);
    }

    // should update link register
    if (instr.Opcode & 0b01)
    {
        ARM_SetReg(14, pc - 3, 0, 0);
    }

    pc += signedimm;

    if (instr.Opcode == 0b10)
    {
        ARM_SetReg(14, pc, 0, 0);
    }
    else
    {
        if (instr.Opcode == 0b01)
        {
            ARM_SetThumb(cpu, false);
            // this might actually be done explicitly?
            addr &= ~0x3;
        }

        ARM_SetReg(15, pc, 0, 0);
    }
}

s8 THUMB9_Branch_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_Branch_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    if (instr.Opcode & 0b01)
    {
        ARM9_CheckInterlocks(ARM9, &stall, 14, 0, false);
    }

    return stall;
}
