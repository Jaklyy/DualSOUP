#pragma once

#include "../../utils.h"




// lsb is technically a negation of the pass/fail
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
    COND_NV, // legacy
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
    VECTOR_RST = 0x00, // Reset
    VECTOR_UND = 0x04, // Undefined Instruction
    VECTOR_SWI = 0x08, VECTOR_SVC = 0x08, // Software Interrupt / Supervisor Call
    VECTOR_PAB = 0x0C, // Prefetch Abort (instruction)
    VECTOR_DAB = 0x10, // Data Abort (data)
    VECTOR_ADR = 0x14, // Address Exception (legacy 26 bit addressing thing, only here for funsies)
    VECTOR_IRQ = 0x18, // Interrupt Request
    VECTOR_FIQ = 0x1C, // Fast Interrupt Request
};

union ARM_FlagsOut
{
    u8 Raw;
    struct
    {
        bool Overflow : 1;
        bool Carry : 1;
        bool Zero : 1;
        bool Negative : 1;
    };
};

union PSR
{
    u32 Raw;
    struct
    {
        u32 Mode : 4;
        u32 ModeMSB : 1; // always set.
        bool Thumb : 1;
        bool FIQDisable : 1;
        bool IRQDisable : 1;
        bool IDABDisable : 1; // v6
        bool DataEndian : 1; // v6
        u32 : 6;
        u32 GE : 4; // v6
        u32 : 4;
        bool Jazelle : 1; // v5tej
        u32 : 2;
        bool QSticky : 1; // armv5te
        bool Overflow : 1;
        bool Carry : 1;
        bool Zero : 1;
        bool Negative : 1;
    };
    struct
    {
        u32 ModeFull : 5;
        u32 : 23;
        u32 Flags : 4;
    };
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
        u32 WakeEvent;
        u16 WakeIRQ;
        struct
        {
            bool InterruptRequest;
            bool FastInterruptRequest;
            bool EventSent; // ARMv6K
        };
    };
    union
    {
        u16 CpuSleeping;
        struct
        {
            bool WaitForInterrupt;
            bool WaitForEvent; // ARMv6K
        };
    };
    union PSR CPSR;
    struct
    {
        u32 R[7];
        union PSR SPSR;
    } FIQ_Bank;
    struct
    {
        u32 R[2];
        union PSR SPSR;
    } IRQ_Bank;
    struct
    {
        u32 R[2];
        union PSR SPSR;
    } SWI_Bank;
    struct
    {
        u32 R[2];
        union PSR SPSR;
    } ABT_Bank;
    struct
    {
        u32 R[2];
        union PSR SPSR;
    } UND_Bank;
    u8 CPUID;
    bool Privileged; // permissions
    u32 Instr[3]; // prefetch pipeline
    timestamp Timestamp;
    struct Console* Sys;
};

bool ARM_ConditionLookup(u8 condition, u8 flags);
void ARM_IncrPC(struct ARM* cpu, bool thumb);
void ARM_BankSwap(struct ARM* cpu, u8 newmode);
void ARM_UpdatePerms(struct ARM* cpu, bool privileged);
void ARM_UpdateMode(struct ARM* cpu, u8 mode);
