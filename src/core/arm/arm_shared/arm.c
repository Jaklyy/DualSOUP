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
