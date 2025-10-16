#pragma once

#include "../../utils.h"




// lsb is technically a negation of the pass/fail
enum ARM_Condition_Codes : u8
{
    ARMCond_EQ,
    ARMCond_NE,
    ARMCond_CS,
    ARMCond_CC,
    ARMCond_MI,
    ARMCond_PL,
    ARMCond_VS,
    ARMCond_VC,
    ARMCond_HI,
    ARMCond_LS,
    ARMCond_GE,
    ARMCond_LT,
    ARMCond_GT,
    ARMCond_LE,
    ARMCond_AL,
    ARMCond_NV, // legacy
};

enum ARM_Modes : u8
{
    ARMMode_USR = 0x0,
    ARMMode_FIQ = 0x1,
    ARMMode_IRQ = 0x2,
    ARMMode_SWI = 0x3, ARMMode_SVC = 0x3,
    ARMMode_ABT = 0x7,
    ARMMode_UND = 0xB,
    ARMMode_SYS = 0xF,
};

enum ARM_Exception_Vector_Offsets : u8
{
    ARMVector_RST = 0x00, // Reset
    ARMVector_UND = 0x04, // Undefined Instruction
    ARMVector_SWI = 0x08, ARMVector_SVC = 0x08, // Software Interrupt / Supervisor Call
    ARMVector_PAB = 0x0C, // Prefetch Abort (instruction)
    ARMVector_DAB = 0x10, // Data Abort (data)
    ARMVector_ADR = 0x14, // Address Exception (legacy 26 bit addressing thing, only here for funsies)
    ARMVector_IRQ = 0x18, // Interrupt Request
    ARMVector_FIQ = 0x1C, // Fast Interrupt Request
};

// proper order of these matters
enum ARM_PipelineProg : u8
{
    ARMPipe_Fetch,
    ARMPipe_Exec,
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

union ARM_PSR
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

struct ARM_Instr
{
    alignas(u64)
    union
    {
        u32 Raw;
        u32 Arm;
        u16 Thumb;
    };
    bool Aborted; // whether the instruction fetch raised a prefetch abort; used for fixing prefetch aborts during flushless switches to thumb, and distinguishing real aborts w/ bkpt
    bool CoprocPriv; // whether the coprocessor pipeline thinks we have privilege or not.
};

#define ARM_CoprocReg(Op1, CRn, CRm, Op2) (((Op1) << 11) | ((CRn) << 7) | ((CRm) << 3) | (Op2))

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
    union ARM_PSR CPSR;
    struct
    {
        u32 R[7];
        union ARM_PSR SPSR;
    } FIQ_Bank;
    struct
    {
        u32 R[2];
        union ARM_PSR SPSR;
    } IRQ_Bank;
    struct
    {
        u32 R[2];
        union ARM_PSR SPSR;
    } SWI_Bank;
    struct
    {
        u32 R[2];
        union ARM_PSR SPSR;
    } ABT_Bank;
    struct
    {
        u32 R[2];
        union ARM_PSR SPSR;
    } UND_Bank;
    u8 CPUID;
    bool Privileged; // permissions
    bool CodeSeq; // should the next code fetch be sequential
    struct ARM_Instr Instr[3]; // prefetch pipeline
    timestamp Timestamp;
    struct Console* Sys;
};

void ARM_Init(struct ARM* cpu, struct Console* sys, const u8 CPUID);
[[nodiscard]] bool ARM_ConditionLookup(const u8 condition, const u8 flags);
void ARM_StepPC(struct ARM* cpu, const bool thumb);
void ARM_BankSwap(struct ARM* cpu, const u8 newmode);
void ARM_UpdatePerms(struct ARM* cpu, const u8 mode);
void ARM_SetMode(struct ARM* cpu, const u8 mode);
void ARM_SetCPSR(struct ARM* cpu, const u32 val);
void ARM_SetThumb(struct ARM* cpu, const bool thumb);
void ARM_PipelineStep(struct ARM* cpu);
void ARM_PipelineFlush(struct ARM* cpu);
