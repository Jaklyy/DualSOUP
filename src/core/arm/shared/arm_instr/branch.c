#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"



union ARM_BranchImm_Decode
{
    u32 Raw;
    struct
    {
        s32 Imm_s24 : 24;
        bool Link : 1;
    };
    struct
    {
        s32 : 24;
        bool HalfwordOffset : 1;
    };
};

void ARM_Branch(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_BranchImm_Decode instr = {.Raw = instr_data.Raw};

    u32 pc = ARM_GetReg(15);
    u32 addr = ((s32)instr.Imm_s24 << 2) + pc;

    ARM_ExeCycles(1, 1, 1);

    if (instr.Link)
        ARM_SetReg(14, pc-4, 0, 0);

    ARM_SetReg(15, addr, 0, 0);
}

// ARMv5
void ARM_BLXImm(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_BranchImm_Decode instr = {.Raw = instr_data.Raw};

    u32 pc = ARM_GetReg(15);
    u32 addr = (((s32)instr.Imm_s24 << 2) | (instr.HalfwordOffset << 1)) + pc;

    ARM_ExeCycles(1, 1, 1);

    // always switches to thumb
    ARM_SetThumb(cpu, true);

    // always saves return address
    ARM_SetReg(14, pc-4, 0, 0);

    ARM_SetReg(15, addr, 0, 0);
}

// ARMv4T (BX)
// ARMv5 (BLX Reg)
union ARM_BranchExchange_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 1;
        bool Link : 1;
    };
};

// TODO: apparently on the ARM7TDMI these are implemented as some sort of unholy MSR?
void ARM_BranchExchange(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_BranchExchange_Decode instr = {.Raw = instr_data.Raw};

    u32 addr = ARM_GetReg(instr.Rm);

    ARM_ExeCycles(1, 1, 1);

    // if bit 0 of the address is set we switch to thumb
    ARM_SetThumb(cpu, addr & 0b1);

    // does not link on arm7
    if (instr.Link && (cpu->CPUID != ARM7ID))
        ARM_SetReg(14, ARM_GetReg(15)-4, 0, 0);

    ARM_SetReg(15, addr, 0, 0);
}

s8 ARM9_BranchExchange_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_BranchExchange_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    return stall;
}
