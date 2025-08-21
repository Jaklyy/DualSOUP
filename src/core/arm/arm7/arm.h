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
    bool CodeSeq; // should the next code fetch be sequential
};

// ensure casting between the two types works as expected
static_assert(offsetof(struct ARM7TDMI, ARM) == 0);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void ARM7_Init(struct ARM7TDMI* ARM7, struct Console* console);

// reset vector exception.
void ARM7_Reset(struct ARM7TDMI* ARM7);

// read register.
[[nodiscard]] u32 ARM7_GetReg(struct ARM7TDMI* ARM7, const int reg);
// write register.
void ARM7_SetReg(struct ARM7TDMI* ARM7, const int reg, u32 val);
// write program counter (r15).
void ARM7_SetPC(struct ARM7TDMI* ARM7, u32 val);

// add execute stage cycles, handle nonsequential code execution.
void ARM7_ExecuteCycles(struct ARM7TDMI* ARM7, const u32 Execute);
