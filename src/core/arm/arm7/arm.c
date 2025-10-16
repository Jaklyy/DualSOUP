#include "../../utils.h"
#include "../shared/arm.h"
#include "arm.h"




#define cpu ((struct ARM*)ARM7)

void ARM7_Init(struct ARM7TDMI* ARM7, struct Console* sys)
{
    ARM_Init(cpu, sys, ARM7ID);
}

void ARM7_Reset(struct ARM7TDMI* ARM7)
{
    // set CPSR
    cpu->CPSR.Mode = ARMMode_SWI;
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->PC = 0x00000000 + ARMVector_RST;
}

union ARM_PSR ARM7_GetSPSR(struct ARM7TDMI* ARM7)
{
    // TODO: THIS IS WRONG FOR ARM7
    switch(cpu->CPSR.Mode)
    {
    case ARMMode_FIQ:
        return cpu->FIQ_Bank.SPSR;
    case ARMMode_IRQ:
        return cpu->IRQ_Bank.SPSR;
    case ARMMode_SWI:
        return cpu->SWI_Bank.SPSR;
    case ARMMode_SWI+1 ... ARMMode_ABT:
        return cpu->ABT_Bank.SPSR;
    case ARMMode_ABT+1 ... ARMMode_UND:
        return cpu->UND_Bank.SPSR;
    case ARMMode_USR:
    case ARMMode_UND+1 ... ARMMode_SYS:
        return cpu->CPSR;
    default: unreachable();
    }
}

void ARM7_SetSPSR(struct ARM7TDMI* ARM7, union ARM_PSR psr)
{
    // TODO: THIS IS WRONG FOR ARM7
    switch(cpu->CPSR.Mode)
    {
    case ARMMode_FIQ:
        cpu->FIQ_Bank.SPSR = psr;
        break;
    case ARMMode_IRQ:
        cpu->IRQ_Bank.SPSR = psr;
        break;
    case ARMMode_SWI:
        cpu->SWI_Bank.SPSR = psr;
        break;
    case ARMMode_SWI+1 ... ARMMode_ABT:
        cpu->ABT_Bank.SPSR = psr;
        break;
    case ARMMode_ABT+1 ... ARMMode_UND:
        cpu->UND_Bank.SPSR = psr;
        break;
    case ARMMode_USR:
    case ARMMode_UND+1 ... ARMMode_SYS:
        // no spsr, no write
        break;
    default: unreachable();
    }
    return;
}

u32 ARM7_GetReg(struct ARM7TDMI* ARM7, const int reg)
{
    // todo: ldm user mode bus contention?

    return cpu->R[reg];
}

void ARM7_SetPC(struct ARM7TDMI* ARM7, u32 val)
{
    /// branch logic???

    // arm7 doesn't seem to implement bit0 of program counter
    // doesn't enforce alignment in arm mode either.
    val &= ~0x1;
    cpu->PC = val;
}

void ARM7_SetReg(struct ARM7TDMI* ARM7, const int reg, u32 val)
{
    // todo: ldm user mode bus contention?

    if (reg == 15) // PC must be handled specially
    {
        ARM7_SetPC(ARM7, val);
    }
    else
    {
        cpu->R[reg] = val;
    }
}

void ARM7_ExecuteCycles(struct ARM7TDMI* ARM7, const u32 Execute)
{
    // must be minus 1 to model pipeline overlaps
    cpu->Timestamp += Execute - 1;
    // internal cycles break up instruction bursts
    // CHECKME: presumably it ends the burst on the first internal cycle?
    if (Execute > 1) cpu->CodeSeq = false;
}

#undef cpu
