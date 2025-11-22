#include <string.h>
#include "../../utils.h"
#include "arm.h"




void ARM_Init(struct ARM* cpu, struct Console* sys, const u8 CPUID)
{
    // set mode id
    cpu->CPUID = CPUID;
    // msb of mode is always set
    cpu->CPSR.ModeMSB = 1;
    cpu->FIQ_Bank.SPSR.ModeMSB = 1;
    cpu->IRQ_Bank.SPSR.ModeMSB = 1;
    cpu->SWI_Bank.SPSR.ModeMSB = 1;
    cpu->ABT_Bank.SPSR.ModeMSB = 1;
    cpu->UND_Bank.SPSR.ModeMSB = 1;
    cpu->Sys = sys;
}

// implementation yoinked nearly 1:1 from melonDS
// this is a fast way to check if a given condition code passes
// 1. Look up an entry in the array using the condition code of the instruction
// 2. Interpret the cpsr flag bitfield as a 4 bit integer to shift
bool ARM_ConditionLookup(const u8 condition, const u8 flags)
{
    constexpr u16 CondLUT[16] =
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

    return CondLUT[condition] & (1<<flags);
}

void ARM_StepPC(struct ARM* cpu, const bool thumb)
{
    cpu->PC += (thumb ? 2 : 4);
}

void ARM_BankSwap(struct ARM* cpu, const u8 newmode)
{
    u8 oldmode = cpu->CPSR.Mode;
    if (oldmode == newmode) return; // this is probably faster but idk

    u8 mode[2] = { oldmode, newmode };

    u32 cpy[7];
    for (int i = 0; i < 2; i++)
    {
        switch(mode[i])
        {
        case ARMMode_FIQ:
            memcpy(cpy, &cpu->FIQ_Bank, 7*4);
            memcpy(&cpu->FIQ_Bank, &cpu->R[8], 7*4);
            memcpy(&cpu->SP, &cpy, 7*4);
            break;
        case ARMMode_IRQ:
            memcpy(cpy, &cpu->IRQ_Bank, 2*4);
            memcpy(&cpu->IRQ_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case ARMMode_SWI:
            memcpy(cpy, &cpu->SWI_Bank, 2*4);
            memcpy(&cpu->SWI_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case ARMMode_ABT:
            memcpy(cpy, &cpu->ABT_Bank, 2*4);
            memcpy(&cpu->ABT_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case ARMMode_UND:
            memcpy(cpy, &cpu->UND_Bank, 2*4);
            memcpy(&cpu->UND_Bank, &cpu->SP, 2*4);
            memcpy(&cpu->SP, &cpy, 2*4);
            break;
        case ARMMode_USR:
        case ARMMode_SYS:
        default: // Invalid modes | TODO: test ARM7?
            break;
        }
    }
}

void ARM_UpdatePerms(struct ARM* cpu, const u8 mode)
{
    cpu->Privileged = mode != ARMMode_USR;
}

void ARM_SetMode(struct ARM* cpu, const u8 mode)
{
    ARM_BankSwap(cpu, mode);
    ARM_UpdatePerms(cpu, mode);
    cpu->CPSR.Mode = mode;
}

void ARM_SetCPSR(struct ARM* cpu, const u32 val)
{
    const union ARM_PSR newcpsr = {.Raw = val};
    ARM_SetMode(cpu, newcpsr.Mode);
    cpu->CPSR = newcpsr;
}

void ARM_SetThumb(struct ARM* cpu, const bool thumb)
{
    cpu->CPSR.Thumb = thumb;
}

void ARM_PipelineStep(struct ARM* cpu)
{
    // step pipeline forward.
    cpu->Instr[0] = cpu->Instr[1];
    cpu->Instr[1] = cpu->Instr[2];
}

// sort of hacky way to handle flushing the pipeline by encoding a condition code failing instruction.
// doing it this way means we no longer need to have dedicated code paths for handling refilling the pipeline.
void ARM_PipelineFlush(struct ARM* cpu)
{
    // if zero flag is set encode not equal, if zero flag is clear encode equal
    constexpr u32 instrs[2][2] = {
        {0xD000, 0xD100}, // thumb: branch cond
        {0x0320F000, 0x1320F000}}; // arm: msr cpsr_(empty field), immediate (v6k: nop) (in theory this one is already a nop but i think encoding a missed instruction would be faster)

    u32 instr = instrs[!cpu->CPSR.Thumb][cpu->CPSR.Zero];
    cpu->Instr[1] = (struct ARM_Instr){.Raw = instr, .Aborted = false, .CoprocPriv = false, .Flushed = true};
    cpu->Instr[2] = (struct ARM_Instr){.Raw = instr, .Aborted = false, .CoprocPriv = false, .Flushed = true};

    cpu->CodeSeq = false;
}
