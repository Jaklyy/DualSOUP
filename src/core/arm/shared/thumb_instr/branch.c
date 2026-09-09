#include "core/utils.h"
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

void THUMB_BranchCond(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_BranchCond_Decode instr = {.Raw = instr_data.Raw};

    // unconditional encodings are repurposed for special purposes
    if (instr.Condition == ARMCond_NV) return ARM_RaiseSVC;
    if (instr.Condition == ARMCond_AL) return ARM_RaiseUDF;

    ARM_ExeCycles(1, 1);

    if (ARM_ConditionLookup(instr.Condition, cpu->CPSR.Flags))
    {
        u32 addr = ARM_GetReg(15);
        addr += (s32)instr.ImmS8 * 2;

        // dont bother stepping pc since we update it anyway.
        ARM_SetReg(15, addr);
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
    struct
    {
        u16 ImmU11 : 11;
    };
};

void THUMB_Branch(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_Branch_Decode instr = {.Raw = instr_data.Raw};

    // 00 = B
    // 01 = BLX hi
    // 10 = lo
    // 11 = BL hi
    u8 opcode = instr.Opcode;

    // TODO: arm7 doesn't have BLX hi.
    // presumably it decodes into BL hi...?
    if ((opcode == 0b01) && (cpu->CPUID == ARM7ID))
        LogPrint(LOG_ARM7 | LOG_ODD, "ARM7 doing a BLX high...?\n");

    // undefined instruction.
    if ((opcode == 0b01) && (instr.ImmU11 & 0x1))
    {
        return ARM_RaiseUDF;
    }

    u32 imm;
    switch(opcode)
    {
    case 0b00: imm = instr.ImmS11 << 1;  break;
    case 0b01: imm = instr.ImmU11 << 1;  break;
    case 0b10: imm = instr.ImmS11 << 12; break;
    case 0b11: imm = instr.ImmU11 << 1;  break;
    }

    // NOTE: the link and pc variables are not used by all variants
    // they're always set though, because im lazy.
    u32 link = ARM_GetReg(15);
    u32 addr = ((opcode == 0b00) ? link : ARM_GetReg(14));

    ARM_ExeCycles(1, 1);

    // update link register
    if (opcode != 0b00) // NOT branch uncond
    {
        if (opcode == 0b10)
        {
            // b(l)x low
            link += imm;
            ARM_StepPC(cpu, true);
        }
        else
        {
            // bl(x) hi
            // this gets the same effect as subtracting 2 and setting the interworking bit
            link -= 1;
        }
        ARM_SetReg(14, link);
    }

    // branch (write to pc)
    if (opcode != 0b10) // BL(X) lo doesn't branch
    {
        addr += imm;

        // blx hi has some extra logic
        if (opcode == 0b01)
        {
            ARM_SetThumb(cpu, false);
            addr &= ~3;
        }

        ARM_SetReg(15, addr);
    }
}

s8 T9ES_Branch_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry [[maybe_unused]])
{
    const union THUMB_Branch_Decode instr = {.Raw = instr_data.Raw};

    // hi variants read lr to use for branching.
    // ... can lr actually be interlocked on thumb?
    if ((instr.Opcode & 0b01) && (14 == reg)) return len;
    return 0;
}
