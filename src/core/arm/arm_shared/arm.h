#pragma once

#include "../../utils.h"




enum ARM_Condition_Codes : u8
{
    COND_EQ,
    COND_NE,
    COND_CS,
    COND_CC,
    COND_MI,
    COND_PL,
    COND_VS,
    COND_VC,
    COND_HI,
    COND_LS,
    COND_GE,
    COND_LT,
    COND_GT,
    COND_LE,
    COND_AL,
    COND_NV,
};

enum ARM_Modes : u8
{
    MODE_USR = 0x0,
    MODE_FIQ = 0x1,
    MODE_IRQ = 0x2,
    MODE_SWI = 0x3, MODE_SVC = 0x3,
    MODE_ABT = 0x7,
    MODE_UND = 0xB,
    MODE_SYS = 0xF,
};

enum ARM_Exception_Vector_Offsets : u8
{
    VECTOR_RST = 0x00,
    VECTOR_UND = 0x04,
    VECTOR_SWI = 0x08, VECTOR_SVC = 0x08,
    VECTOR_PAB = 0x0C,
    VECTOR_DAB = 0x10,

    VECTOR_IRQ = 0x18,
    VECTOR_FIQ = 0x1C,
};

struct ARM
{
    alignas(HOST_CACHEALIGN)
    union
    {
        u32 R[16];
        struct
        {
            u32 R0; u32 R1; u32 R2; u32 R3; u32 R4; u32 R5; u32 R6; u32 R7; u32 R8; u32 R9; u32 R10; u32 R11; u32 R12;
            union { u32 R13; u32 SP; };
            union { u32 R14; u32 LR; };
            union { u32 R15; u32 PC; };
        };
    };
    union
    {
        u32 Data;
        struct
        {
            u32 Mode : 4;
            u32 : 1; // technically msb of mode; always set.
            bool Thumb : 1;
            bool FIQDisable : 1;
            bool IRQDisable : 1;
            u32 : 19;
            bool QSticky : 1; // armv5te
            bool Overflow : 1;
            bool Carry : 1;
            bool Zero : 1;
            bool Negative : 1;
        };
        struct
        {
            u32 : 28;
            u32 Flags : 4;
        };
    } CPSR;
    u32* SPSR;
    u64 CycleCount;
    struct Console* Console;
};

bool ARM_ConditionLookup(u8 condition, u8 flags);
void ARM_IncrPC(struct ARM* cpu, bool thumb);
//void ARM_AddCycles(struct ARM* cpu, int cycles);
