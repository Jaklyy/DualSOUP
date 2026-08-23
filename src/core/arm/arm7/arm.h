#pragma once

#include <stddef.h>
#include "../../utils.h"
#include "../shared/arm.h"



// NDS model: ARM7TDMI (unknown revision?) (i'm speculating its Rev 4, but its unconfirmed)
// GBA model: ARM7TDMI Rev 3A

/*
    name decodes as:
    7: Orange
    T: Thumb
    D: Something debugging related
    M: Fast Multiplier (it was so good they removed it from the ARM946E-S)
    I: Debugging but different
*/


typedef struct
{
    ARM ARM;
} ARM7TDMI;

// ensure casting between the two types works as expected
static_assert(offsetof(ARM7TDMI, ARM) == 0);

extern void (*ARM7_InstructionLUT[0x1000])(ARM*, ARM_Instr);
extern void (*THUMB7_InstructionLUT[64])(ARM*, ARM_Instr);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void A7TDMI_Init(ARM7TDMI* a7tdmi, Console* console);

// arm7 handler entrypoint
void ARM7_MainLoop(ARM7TDMI* a7tdmi);

// special exceptions
void ARM7_Reset(ARM7TDMI* a7tdmi);
void ARM7_InterruptRequest(ARM7TDMI* a7tdmi);
// only used by debug hardware
void ARM7_FastInterruptRequest(ARM7TDMI* a7tdmi);

void ARM7_RaiseUDF(ARM* ARM, const ARM_Instr instr_data, const int cycles);
// executed exceptions
void ARM7_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data);
void ARM7_SoftwareInterrupt(ARM* ARM, const ARM_Instr instr_data);
// copies for thumb
void THUMB7_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data);
void THUMB7_SoftwareInterrupt(ARM* ARM, const ARM_Instr instr_data);

[[nodiscard]] union ARM_PSR ARM7_GetSPSR(ARM7TDMI* ARM7);
void ARM7_SetSPSR(ARM7TDMI* ARM7, union ARM_PSR psr);

// read register.
[[nodiscard]] u32 ARM7_GetReg(ARM7TDMI* ARM7, const int reg);
// write register.
void ARM7_SetReg(ARM7TDMI* ARM7, const int reg, u32 val);
// write program counter (r15).
void ARM7_SetPC(ARM7TDMI* ARM7, u32 val);

// add execute stage cycles, handle nonsequential code execution.
void ARM7_ExecuteCycles(ARM7TDMI* ARM7, const u32 Execute);

[[nodiscard]] u32 ARM7_DataRead32(ARM7TDMI* ARM7, const u32 addr, bool* seq);
[[nodiscard]] u32 ARM7_DataRead16(ARM7TDMI* ARM7, const u32 addr, bool* seq);
[[nodiscard]] u32 ARM7_DataRead8(ARM7TDMI* ARM7, const u32 addr, bool* seq);
void ARM7_BusWrite(ARM7TDMI* ARM7, const u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq);
void ARM7_DataWrite32(ARM7TDMI* ARM7, const u32 addr, u32 val, const bool atomic, bool* seq);
void ARM7_DataWrite16(ARM7TDMI* ARM7, const u32 addr, u32 val, bool* seq);
void ARM7_DataWrite8(ARM7TDMI* ARM7, const u32 addr, u32 val, const bool atomic, bool* seq);
void ARM7_InstrRead32(ARM7TDMI* ARM7, const u32 addr);
void ARM7_InstrRead16(ARM7TDMI* ARM7, const u32 addr);

// temp
void ARM7_Log(ARM7TDMI* ARM7);
