#include "../../utils.h"
#include "../shared/arm.h"
#include "../../console.h"
#include "arm.h"




#define cpu ((struct ARM*)ARM7)

void ARM7_Init(struct ARM7TDMI* ARM7, struct Console* sys)
{
    ARM_Init(cpu, sys, ARM7ID);
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

    ARM_PipelineFlush(cpu);
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
    cpu->CodeSeq = (Execute == 1);
}

#define FetchIRQExec(size, x) \
/* Step 2: Fetch upcoming instruction. */ \
ARM7_InstrRead##size (ARM7, cpu->PC); \
/* Step 3: Check if an IRQ should be raised. */ \
if (!ARM7_CheckInterrupts(ARM7)) \
{ \
    /* Step 4: Execute the next instruction. */ \
    x ; \
}

[[nodiscard]] bool ARM7_CheckInterrupts(struct ARM7TDMI* ARM7)
{
    if (ARM7->ARM.Timestamp >= Console_GetARM9Cur(ARM7->ARM.Sys))
        CR_Switch(ARM7->ARM.Sys->HandleARM9);

    if (cpu->WakeIRQ)
    {
#if 0
        if (cpu->FastInterruptRequest) // jakly why are you implementing this...
        {
            ARM9_FastInterruptRequest(ARM9);
            return true;
        }
        else
#endif
        {
            ARM7_InterruptRequest(ARM7);
            return true;
        }
    }
    else return false;
}

// TEMP: debugging
void ARM7_Log(struct ARM7TDMI* ARM7)
{
    LogPrint(LOG_ARM7, "DUMPING ARM7 STATE:\n");
    for (int i = 0; i < 16; i++)
    {
        LogPrint(LOG_ARM7, "R%2i: %08X ", i, cpu->R[i]);
    }
    //LogPrint(LOG_ARM9, "R2:%08X\n", cpu->R[2]);
    LogPrint(LOG_ARM7, "CPSR:%08X\n", cpu->CPSR.Raw);
    if (cpu->Instr[0].Flushed)
    {
        LogPrint(LOG_ARM7, "INSTR: Flushed. ");
    }
    else
    {
        LogPrint(LOG_ARM7, "INSTR: %08X ", cpu->Instr[0]);
    }
    LogPrint(LOG_ARM7, "EXE:%li\n\n", cpu->Timestamp);
}

void ARM7_Step(struct ARM7TDMI* ARM7)
{
    // step the pipeline.
    ARM_PipelineStep(cpu);

    if (cpu->CPSR.Thumb)
    {
        const struct ARM_Instr instr = cpu->Instr[0];
        const u16 decode = (instr.Thumb >> 10);

        FetchIRQExec(16, THUMB7_InstructionLUT[decode](cpu, instr))
    }
    else
    {
        const struct ARM_Instr instr = cpu->Instr[0];
        const u8 condcode = instr.Arm >> 28;
        const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

        // first we need to check the condition code (should be part of decoding?)
        if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
        {
            FetchIRQExec(32, ARM7_InstructionLUT[decode](cpu, instr))
        }
        else // failed the condition check.
        {
            ARM7_InstrRead32(ARM7, cpu->PC);

            // this needs a special check because im stupid and reusing this path for pipeline refills
            if (!cpu->Instr[0].Flushed && !ARM7_CheckInterrupts(ARM7))
            {
                ARM7_ExecuteCycles(ARM7, 1);
            }
            ARM_StepPC(cpu, false);
        }
    }

    // TEMP: debugging
    //ARM7_Log(ARM7);
}

#undef ILCheck
#undef FetchIRQExecute

void ARM7_MainLoop(struct ARM7TDMI* ARM7)
{
    while(true)
    {
        if (cpu->Timestamp >= cpu->Sys->ARM7Target)
            CR_Switch(cpu->Sys->HandleMain);
        else
            ARM7_Step(ARM7);
    }
}

#undef cpu
