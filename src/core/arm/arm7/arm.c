#include "core/scheduler.h"
#include "core/utils.h"
#include "../shared/arm.h"
#include "core/console.h"
#include "arm.h"




#define cpu ((ARM*)a7tdmi)

// TEMP: debugging
void A7TDMI_Log(ARM7TDMI* a7tdmi [[maybe_unused]])
{
#if 0
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
#endif
}

void A7TDMI_Init(ARM7TDMI* a7tdmi, Console* sys)
{
    ARM_Init(cpu, sys, ARM7ID);
}

ARM_PSR A7TDMI_GetSPSR(ARM7TDMI* a7tdmi)
{
    // TODO: THIS IS WRONG FOR ARM7
    switch(cpu->CPSR.Mode)
    {
    case ARMMode_FIQ:
        return cpu->FIQ_Bank.SPSR;
    case ARMMode_IRQ:
        return cpu->IRQ_Bank.SPSR;
    case ARMMode_SVC:
        return cpu->SVC_Bank.SPSR;
    case ARMMode_SVC+1 ... ARMMode_ABT:
        return cpu->ABT_Bank.SPSR;
    case ARMMode_ABT+1 ... ARMMode_UND:
        return cpu->UND_Bank.SPSR;
    case ARMMode_USR:
    case ARMMode_UND+1 ... ARMMode_SYS:
        return cpu->CPSR;
    default: unreachable();
    }
}

void A7TDMI_SetSPSR(ARM7TDMI* a7tdmi, ARM_PSR psr)
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
    case ARMMode_SVC:
        cpu->SVC_Bank.SPSR = psr;
        break;
    case ARMMode_SVC+1 ... ARMMode_ABT:
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

u32 A7TDMI_GetReg(ARM7TDMI* a7tdmi, const int reg)
{
    // todo: ldm user mode bus contention?

    return cpu->R[reg];
}

void A7TDMI_SetPC(ARM7TDMI* a7tdmi, u32 val)
{
    // arm7 doesn't seem to implement bit0 of program counter
    // and doesn't enforce alignment in arm mode.
    val &= ~0x1;
    if ((val & 2) && !cpu->CPSR.Thumb) LogPrint(LOG_ARM7|LOG_ODD, "ARM7: Misaligned branch in ARM mode.\n");
    cpu->PC = val;
    cpu->FlushProg = 3;
}

void A7TDMI_SetReg(ARM7TDMI* a7tdmi, const int reg, u32 val)
{
    // todo: ldm user mode bus contention?
    if (reg == 15) // writes to PC need special handling
        A7TDMI_SetPC(a7tdmi, val);
    else
        cpu->R[reg] = val;
}

void A7TDMI_ExecuteCycles(ARM7TDMI* a7tdmi, const u32 execute)
{
    cpu->Timestamp += DSClk33(execute);
    // NOTE: internally arm7tdmi instruction bursts are weird due to mixed sequential + idle cycles?
    // they seem to just be handled as nonsequential though...
    cpu->CodeSeq = (execute == 0);
}

[[nodiscard]] bool A7TDMI_CheckInterrupts(ARM7TDMI* a7tdmi)
{
    if (!cpu->CPSR.IRQDisable && cpu->InterruptRequest)
    {
        A7TDMI_InterruptRequest(a7tdmi);
        return true;
    }
    else return false;
}

void A7TDMI_Exec(ARM7TDMI* a7tdmi)
{
    if (!A7TDMI_CheckInterrupts(a7tdmi))
    {
        if (cpu->CPSR.Thumb)
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u16 decode = (instr.Thumb >> 10);

            T7TDMI_InstructionLUT[decode](cpu, instr);
        }
        else
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u8 condcode = instr.Arm >> 28;
            const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

            // first we need to check the condition code (should be part of decoding?)
            if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
                A7TDMI_InstructionLUT[decode](cpu, instr);
            else // failed the condition check.
            {
                A7TDMI_ExecuteCycles(a7tdmi, 0);
                ARM_StepPC(cpu, false);
            }
        }
    }
}

void A7TDMI_Run(ARM7TDMI* a7tdmi, timestamp now)
{
    cpu->Timestamp = now;

    A7TDMI_Exec(a7tdmi);

    if (!a7tdmi->BusGo)
        A7TDMI_InstrRead(a7tdmi, cpu->Timestamp);

    ARM_PipelineStep(cpu);
}
#undef cpu
