#include "../../utils.h"
#include "../shared/arm.h"
#include "../../console.h"
#include "arm.h"




#define cpu ((struct ARM*)ARM7)

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
    LogPrint(LOG_ARM7, "INSTR: %08X ", cpu->Instr[0].Raw);
    LogPrint(LOG_ARM7, "EXE:%li\n\n", cpu->Timestamp);
    LogPrint(LOG_ARM7, "%08X %08X %i\n", cpu->Sys->IF7, cpu->Sys->IE7, cpu->Sys->IME7);
    LogPrint(LOG_ARM7, "%08X\n", cpu->Sys->Timers7[3].CR.Raw);
}

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
    // arm7 doesn't seem to implement bit0 of program counter
    // and doesn't enforce alignment in arm mode.
    val &= ~0x1;
    if (val & 2 && cpu->CPSR.Thumb) LogPrint(LOG_ARM7|LOG_ODD, "ARM7: Misaligned branch in ARM mode.\n");
    cpu->PC = val;
    cpu->Prog = ARMProg_RefillStart;
}

void ARM7_SetReg(struct ARM7TDMI* ARM7, const int reg, u32 val)
{
    // todo: ldm user mode bus contention?

    if (reg == 15) // writes to PC need special handling
    {
        ARM7_SetPC(ARM7, val);
    }
    else
    {
        cpu->R[reg] = val;
    }
}

void ARM7_ExecuteCycles(struct ARM7TDMI* ARM7, const u32 execute)
{
    // must be minus 1 to model pipeline overlaps
    cpu->Timestamp += execute - 1;
    // internal cycles break up instruction bursts
    // CHECKME: presumably it ends the burst on the first internal cycle?
    cpu->CodeSeq = (execute == 1);
}

[[nodiscard]] bool ARM7_CheckInterrupts(struct ARM7TDMI* ARM7)
{
    //Scheduler_Sync(cpu->Sys, cpu->Timestamp, Sync_Normal7);

    // TODO: schedule this instead
    if (cpu->Sys->IME7 && !cpu->CPSR.IRQDisable && (cpu->Sys->IE7 & cpu->Sys->IF7))
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

void ARM7_Fetch(struct ARM7TDMI* ARM7)
{
    // step the pipeline.
    ARM_PipelineStep(cpu);

    // begin instruction fetch
    if (cpu->CPSR.Thumb)
        ARM7_InstrRead16(ARM7, cpu->PC);
    else
        ARM7_InstrRead32(ARM7, cpu->PC);
}

void ARM7_Exec(struct ARM7TDMI* ARM7)
{
    if (!ARM7_CheckInterrupts(ARM7))
    {
        if (cpu->CPSR.Thumb)
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u16 decode = (instr.Thumb >> 10);

            THUMB7_InstructionLUT[decode](cpu, instr);
        }
        else
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u8 condcode = instr.Arm >> 28;
            const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

            // first we need to check the condition code (should be part of decoding?)
            if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
            {
                ARM7_InstructionLUT[decode](cpu, instr);
            }
            else // failed the condition check.
            {
                ARM7_ExecuteCycles(ARM7, 1);
                ARM_StepPC(cpu, false);
            }
        }
    }

    cpu->Prog = ARMProg_SleepCheck;
}

void ARM7_MainLoop(struct ARM7TDMI* ARM7)
{
    switch(cpu->Prog)
    {
        case ARMProg_Sleep:
        {
                // TODO?
            //if (!Console_CheckARM7Wake(cpu->Sys))
            {
                return;
            }
            //cpu->Prog = ARMProg_Fetch;
            //[[fallthrough]]; // checkme?
        }
        case ARMProg_RefillStart:
        case ARMProg_RefillMid:
        case ARMProg_Fetch:
        {
            cpu->Prog += 1; static_assert((((ARMProg_RefillStart + 1) == ARMProg_RefillMid) && ((ARMProg_RefillMid + 1) == ARMProg_Fetch) && ((ARMProg_Fetch + 1) == ARMProg_Exec)), "ARM PROG NEEDS ADJUSTING HERE");

            ARM7_Fetch(ARM7);
            break;
        }
        case ARMProg_Exec:
        {
            ARM7_Exec(ARM7);
            break;
        }
        case ARMProg_BusWait:
        {
            break;
        }
        case ARMProg_SleepCheck:
        {
            if (cpu->CpuSleeping) // is it more correct to do this at the start of a step? does it even matter?
            {
                if (Console_CheckARM7Wake(cpu->Sys))
                {
                    cpu->CpuSleeping = 0;
                }
                else
                {
                    cpu->Prog = ARMProg_Sleep;
                }
            }
            break;
        }
    }
}

#undef cpu
