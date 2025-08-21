#include "../../utils.h"
#include "../shared/arm.h"
#include "arm.h"




#define cpu ((struct ARM*)ARM7)

void ARM7_Init(struct ARM7TDMI* ARM7, struct Console* sys)
{
    cpu->CPUID = ARM7ID;
    // msb of mode is always set
    cpu->CPSR.ModeMSB = 1;
    cpu->Sys = sys; // TODO

    ARM7_Reset(ARM7); // raise reset exception
}

void ARM7_Reset(struct ARM7TDMI* ARM7)
{
    // set CPSR
    cpu->CPSR.Mode = MODE_SWI;
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->PC = 0x00000000 + VECTOR_RST;
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
    cpu->Timestamp += Execute;
    if (Execute != 0) ARM7->CodeSeq = false;
}

#undef cpu
