#pragma once

#include <stddef.h>
#include "../../utils.h"
#include "../shared/arm.h"



// exact model: ARM7TDMI (what revision?)

/*
    name decodes as:
    7: Orange
    T: Thumb
    D: Something debugging related
    M: Fast Multiplier (it was so good they removed it from the ARM946E-S)
    I: Debugging but different
*/


struct ARM7TDMI
{
    struct ARM ARM;
};

// ensure casting between the two types works as expected
static_assert(offsetof(struct ARM7TDMI, ARM) == 0);

extern void (*ARM7_InstructionLUT[0x1000])(struct ARM*, struct ARM_Instr);
extern void (*THUMB7_InstructionLUT[0x1000])(struct ARM*, struct ARM_Instr);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void ARM7_Init(struct ARM7TDMI* ARM7, struct Console* console);

// arm7 handler entrypoint
void ARM7_MainLoop(struct ARM7TDMI* ARM7);

// special exceptions
void ARM7_Reset(struct ARM7TDMI* ARM7);
void ARM7_InterruptRequest(struct ARM7TDMI* ARM7);
// only used by debug hardware
void ARM7_FastInterruptRequest(struct ARM7TDMI* ARM7);

void ARM7_RaiseUDF(struct ARM* ARM, const struct ARM_Instr instr_data, const int cycles);
// executed exceptions
void ARM7_UndefinedInstruction(struct ARM* ARM, const struct ARM_Instr instr_data);
void ARM7_SoftwareInterrupt(struct ARM* ARM, const struct ARM_Instr instr_data);
// copies for thumb
void THUMB7_UndefinedInstruction(struct ARM* ARM, const struct ARM_Instr instr_data);
void THUMB7_SoftwareInterrupt(struct ARM* ARM, const struct ARM_Instr instr_data);

[[nodiscard]] union ARM_PSR ARM7_GetSPSR(struct ARM7TDMI* ARM7);
void ARM7_SetSPSR(struct ARM7TDMI* ARM7, union ARM_PSR psr);

// read register.
[[nodiscard]] u32 ARM7_GetReg(struct ARM7TDMI* ARM7, const int reg);
// write register.
void ARM7_SetReg(struct ARM7TDMI* ARM7, const int reg, u32 val);
// write program counter (r15).
void ARM7_SetPC(struct ARM7TDMI* ARM7, u32 val);

// add execute stage cycles, handle nonsequential code execution.
void ARM7_ExecuteCycles(struct ARM7TDMI* ARM7, const u32 Execute);

[[nodiscard]] u32 ARM7_BusRead(struct ARM7TDMI* ARM7, const u32 addr, const u32 mask, bool* seq);
void ARM7_BusWrite(struct ARM7TDMI* ARM7, const u32 addr, const u32 val, const u32 mask, const u32 atomic, bool* seq);
void ARM7_InstrRead32(struct ARM7TDMI* ARM7, const u32 addr);
void ARM7_InstrRead16(struct ARM7TDMI* ARM7, const u32 addr);
