#pragma once

#include <stddef.h>
#include "core/utils.h"
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

extern void (*A7TDMI_InstructionLUT[0x1000])(ARM*, ARM_Instr);
extern void (*T7TDMI_InstructionLUT[64])(ARM*, ARM_Instr);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void A7TDMI_Init(ARM7TDMI* a7tdmi, Console* console);

// arm7 handler entrypoint
void A7TDMI_Run(ARM7TDMI* a7tdmi);

// special exceptions
void A7TDMI_Reset(ARM7TDMI* a7tdmi);
void A7TDMI_InterruptRequest(ARM7TDMI* a7tdmi);
// only used by debug hardware
void A7TDMI_FastInterruptRequest(ARM7TDMI* a7tdmi);

void A7TDMI_RaiseUDF(ARM* ARM, const ARM_Instr instr_data, const int cycles);
// executed exceptions
void A7TDMI_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data);
void A7TDMI_SupervisorCall(ARM* ARM, const ARM_Instr instr_data);
// copies for thumb
void T7TDMI_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data);
void T7TDMI_SupervisorCall(ARM* ARM, const ARM_Instr instr_data);

[[nodiscard]] ARM_PSR A7TDMI_GetSPSR(ARM7TDMI* a7tdmi);
void A7TDMI_SetSPSR(ARM7TDMI* a7tdmi, ARM_PSR psr);

// read register.
[[nodiscard]] u32 A7TDMI_GetReg(ARM7TDMI* a7tdmi, const int reg);
// write register.
void A7TDMI_SetReg(ARM7TDMI* a7tdmi, const int reg, u32 val);
// write program counter (r15).
void A7TDMI_SetPC(ARM7TDMI* a7tdmi, u32 val);

// add execute stage cycles, handle nonsequential code execution.
void A7TDMI_ExecuteCycles(ARM7TDMI* a7tdmi, const u32 Execute);

void A7TDMI_RotateExtendUnit(u32* rdata, const u32 addr, const ARM_DataWidth size, const bool signext);
[[nodiscard]] u32 A7TDMI_DataRead32(ARM7TDMI* a7tdmi, const u32 addr, bool* seq);
[[nodiscard]] u32 A7TDMI_DataRead16(ARM7TDMI* a7tdmi, const u32 addr, bool* seq);
[[nodiscard]] u32 A7TDMI_DataRead8(ARM7TDMI* a7tdmi, const u32 addr, bool* seq);
void A7TDMI_BusWrite(ARM7TDMI* a7tdmi, const u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq);
void A7TDMI_DataWrite32(ARM7TDMI* a7tdmi, const u32 addr, u32 val, const bool atomic, bool* seq);
void A7TDMI_DataWrite16(ARM7TDMI* a7tdmi, const u32 addr, u32 val, bool* seq);
void A7TDMI_DataWrite8(ARM7TDMI* a7tdmi, const u32 addr, u32 val, const bool atomic, bool* seq);
void A7TDMI_InstrRead32(ARM7TDMI* a7tdmi, const u32 addr);
void A7TDMI_InstrRead16(ARM7TDMI* a7tdmi, const u32 addr);

// temp
void A7TDMI_Log(ARM7TDMI* a7tdmi);
