#include "arm.h"
#include "../arm_shared/arm.h"
#include "../../utils.h"


#define cpu ((struct ARM*)ARM9)

void ARM9_PipelineFlush(struct ARM946E_S* ARM9)
{
    cpu->PC &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3); // ensure program counter is aligned; im not 100% sure where this is enforced?
}

u32 ARM9_GetExceptionBase(struct ARM946E_S* ARM9)
{
    // TODO: actually do this properly
    return 0xFFFF0000;
}

void ARM9_Reset(struct ARM946E_S* ARM9)
{
    cpu->CPSR.Mode = MODE_SWI;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;
    // checkme: should the exception vector base be reset too?
    cpu->PC = ARM9_GetExceptionBase(ARM9) + VECTOR_RST;
    ARM9_PipelineFlush(ARM9);
}

void ARM9_Init(struct ARM946E_S* ARM9)
{
    cpu->CPSR.Data |= 1<<4; // ensure msb of mode bit is set properly
    ARM9_Reset(ARM9); // raise reset exception
}

u32 ARM9_GetReg(struct ARM946E_S* ARM9, const int reg, const int cycledelay, const bool readportc)
{
    // todo: strd/ldrd incorrect forwarding errata

    // interlock
    if (ARM9->RegIL[readportc][reg] > cpu->CycleCount+cycledelay)
    {
        cpu->CycleCount = ARM9->RegIL[readportc][reg];
    }

    return cpu->R[reg];
}

void ARM9_AddCycles(struct ARM946E_S* ARM9, const int Execute, const int Memory)
{
    
}

#undef cpu
