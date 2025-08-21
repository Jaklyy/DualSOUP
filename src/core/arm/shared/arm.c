#include <string.h>
#include "../../utils.h"
#include "arm.h"




// implementation yoinked nearly 1:1 from melonDS
// this is a fast way to check if a given condition code passes
// 1. Look up an entry in the array using the condition code of the instruction
// 2. Interpret the cpsr flag bitfield as a 4 bit integer to shift
alignas(32) constexpr u16 CondLUT[16] =
{
    0xF0F0, // EQ - Zero set
    0x0F0F, // NE - Zero clear
    0xCCCC, // CS - Carry set
    0x3333, // CC - Carry clear
    0xFF00, // MI - Negative set
    0x00FF, // PL - Negative clear
    0xAAAA, // VS - Overflow set
    0x5555, // VC - Overflow clear
    0x0C0C, // HI - Carry set && Zero clear
    0xF3F3, // LS - Carry clear && Zero set
    0xAA55, // GE - Negative == Overflow
    0x55AA, // LT - Negative != Overflow
    0x0A05, // GT - Zero clear && (Negative == Overflow)
    0xF5FA, // LE - Zero set && (Negative != Overflow)
    0xFFFF, // AL - Always passes
    0x0000  // NV - Never passes (ARM4T and earlier?) | Unconditional instruction (ARMv5TE and onward)
};

bool ARM_ConditionLookup(u8 condition, u8 flags)
{
    return CondLUT[condition] & (1<<flags);
}

void ARM_IncrPC(struct ARM* cpu, bool thumb)
{
    cpu->PC += (thumb ? 2 : 4);
}

void ARM_BankSwap(struct ARM* cpu, u8 newmode)
{
    u8 oldmode = cpu->CPSR.Mode;
    if (oldmode == newmode) return; // this is probably faster but idk

    u8 mode[2] = { oldmode, newmode };

    u32 cpy[7];
    for (int i = 0; i < 2; i++)
    {
        switch(mode[i])
        {
        case MODE_FIQ:
            memcpy(cpy, &cpu->FIQ_Bank, 7*4);
            memcpy(&cpu->FIQ_Bank, &cpu->R[8], 7*4);
            memcpy(&cpu->SP, &cpy, 7*4);
            break;
        case MODE_IRQ:
            memcpy(cpy, &cpu->IRQ_Bank, 2*4);
            memcpy(&cpu->IRQ_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case MODE_SWI:
            memcpy(cpy, &cpu->SWI_Bank, 2*4);
            memcpy(&cpu->SWI_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case MODE_ABT:
            memcpy(cpy, &cpu->ABT_Bank, 2*4);
            memcpy(&cpu->ABT_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case MODE_UND:
            memcpy(cpy, &cpu->UND_Bank, 2*4);
            memcpy(&cpu->UND_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case MODE_USR:
        case MODE_SYS:
        default: // Invalid modes | TODO: test ARM7?
            break;
        }
    }
}

void ARM_UpdatePerms(struct ARM* cpu, bool privileged)
{
    cpu->Privileged = privileged;
}

void ARM_UpdateMode(struct ARM* cpu, u8 mode)
{
    ARM_BankSwap(cpu, mode);
    cpu->CPSR.Mode = mode;
    ARM_UpdatePerms(cpu, mode != MODE_USR);
}
