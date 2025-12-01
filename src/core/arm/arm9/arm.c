#include "../../utils.h"
#include "../shared/arm.h"
#include "../../console.h"
#include "instr_il.h"
#include "arm.h"




#define cpu ((struct ARM*)ARM9)

// TEMP: debugging
void ARM9_Log(struct ARM946ES* ARM9)
{
    LogPrint(LOG_ARM9, "DUMPING ARM9 STATE:\n");
    for (int i = 0; i < 16; i++)
    {
        LogPrint(LOG_ARM9, "R%2i: %08X ", i, cpu->R[i]);
    }
    //LogPrint(LOG_ARM9, "R2:%08X\n", cpu->R[2]);
    LogPrint(LOG_ARM9, "CPSR:%08X\n", cpu->CPSR.Raw);
    if (cpu->Instr[0].Flushed)
    {
        LogPrint(LOG_ARM9, "INSTR: Flushed. ");
    }
    else
    {
        LogPrint(LOG_ARM9, "INSTR: %08X ", cpu->Instr[0]);
    }
    LogPrint(LOG_ARM9, "EXE:%li MEM:%li\n\n", cpu->Timestamp, ARM9->MemTimestamp);
}

void ARM9_Init(struct ARM946ES* ARM9, struct Console* sys)
{
    ARM_Init(cpu, sys, ARM9ID);

    // set permanently set CP15 CR bits
    ARM9->CP15.CR.FixedOnes = 0xF;

    // 7 indicates no cache streaming in progress
    ARM9->DStream.Prog = 7;
    ARM9->IStream.Prog = 7;
    ARM9->CP15.DCachePRNG = 0x0123456789ABCDEF;
    ARM9->CP15.ICachePRNG = 0xFEDCBA9876543210;
    // finally something being initialized that isn't a constant!
    // this needs to not be 0, because a lot of logic relies on this timestamp - 1
    //ARM9->MemTimestamp = 1; nvm we dont need this actually i was dumb
}

union ARM_PSR ARM9_GetSPSR(struct ARM946ES* ARM9)
{
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

void ARM9_SetSPSR(struct ARM946ES* ARM9, union ARM_PSR psr)
{
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

u32 ARM9_GetReg(struct ARM946ES* ARM9, const int reg)
{
    // todo: strd/ldrd incorrect forwarding errata

    return cpu->R[reg];
}

// interlocks on ARM946E-S:
// execute stage interlocks delay the fetch stage which delayes the execute stage
// memory stage interlocks do... something?
// base is memory stage end - 1
// 

void ARM9_UpdateInterlocks(struct ARM946ES* ARM9, const s8 diff)
{
    // i spent some time writing simd for this manually but it's so simple auto-simd was just as good.
    for (int i = 0; i < 32; i++)
    {
        ARM9->RegIL[i & 0xF][i>>4] -= diff;
        if (ARM9->RegIL[i & 0xF][i>>4] < 0) ARM9->RegIL[i & 0xF][i>>4] = 0;
    }
}

void ARM9_InterlockStall(struct ARM946ES* ARM9, const s8 stall)
{
    if (stall > 0)
    {
        cpu->Timestamp = ARM9->MemTimestamp + stall - 1;
        ARM9_UpdateInterlocks(ARM9, stall);
    }
}

void ARM9_SetPC(struct ARM946ES* ARM9, u32 addr, const s8 iloffs)
{
    // TEMP: debugging
    //ARM9_DumpMPU(ARM9);
    //ARM9_Log(ARM9);

    // arm9 enforces pc alignment properly for once.
    addr &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3);
    // r15 interlocks must be resolved immediately.
    ARM9_InterlockStall(ARM9, iloffs);

    cpu->PC = addr;

    ARM_PipelineFlush(cpu);
}

void ARM9_SetReg(struct ARM946ES* ARM9, const int reg, u32 val, const s8 iloffs, const s8 iloffs_c)
{
    if (reg == 15) // PC must be handled specially
    {
        ARM9_SetPC(ARM9, val, iloffs); // CHECKME: should this be the port C time?
    }
    else
    {
        // I pray that nothing makes it any more complex than this.
        cpu->R[reg] = val;
        ARM9->RegIL[reg][0] = iloffs;
        ARM9->RegIL[reg][1] = iloffs_c;
    }
}

void ARM9_CheckInterlocks(struct ARM946ES* ARM9, s8* stall, const int reg, const s8 cycledelay, const bool portc)
{
    // the fact this always needs a branch really annoys me.
    // but i dont think this is possible to work around without losing accuracy.
    s8 diff = ARM9->RegIL[reg][portc] - cycledelay;
    if (*stall < diff) *stall = diff;
}

void ARM9_FetchCycles(struct ARM946ES* ARM9, const int fetch)
{
    cpu->Timestamp += fetch;

    // next instruction cannot execute until the last memory stage is complete
    if (cpu->Timestamp < (ARM9->MemTimestamp))
        cpu->Timestamp = (ARM9->MemTimestamp);
}

void ARM9_ExecuteCycles(struct ARM946ES* ARM9, const int execute, const int memory)
{
    // execute cycles must be minus 1 due to how im handling pipeline overlaps
    cpu->Timestamp += execute - 1;

    // catch the memory timestamp up
    // save the difference between old and new so we can also catch up the interlock timestamps
    s8 diff = (cpu->Timestamp + memory) - ARM9->MemTimestamp;
    ARM9->MemTimestamp += diff;

    ARM9_UpdateInterlocks(ARM9, diff);

    cpu->CodeSeq = true;
}

void ARM9_ExecuteOnly(struct ARM946ES* ARM9, const int execute)
{
    // TODO
}

#define ILCheck(size, x) \
/* Step 1: Handle interlocks. */ \
s8 stall = x (ARM9, instr); \
if (stall) \
{ \
    ARM9_InterlockStall(ARM9, stall); \
}

#define FetchIRQExec(size, x) \
/* Step 2: Fetch upcoming instruction. */ \
ARM9_InstrRead##size (ARM9, cpu->PC); \
/* Step 3: Check if an IRQ should be raised. */ \
if (!ARM9_CheckInterrupts(ARM9)) \
{ \
    /* Step 4: Execute the next instruction. */ \
    x ; \
}

[[nodiscard]] bool ARM9_CheckInterrupts(struct ARM946ES* ARM9)
{
    // TODO: fix for dsi mode
    Console_SyncWith7GT(cpu->Sys, cpu->Timestamp/2);

    // todo: schedule this instead
    if (cpu->Sys->IME9 && !cpu->CPSR.IRQDisable && (cpu->Sys->IE9 & cpu->Sys->IF9))
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
            ARM9_InterruptRequest(ARM9);
            return true;
        }
    }
    else return false;
}

/*  order of operations:
    1. pipeline is stepped
    2. interlocks are stalled for
    3. code is fetched
    4. irqs are checked
    5. instruction is executed
*/
void ARM9_Step(struct ARM946ES* ARM9)
{
    ARM9_CatchUpWriteBuffer(ARM9, &ARM9->ARM.Timestamp);

    if (cpu->CpuSleeping)
    {
        printf("9zzzz\n");
        cpu->Timestamp = cpu->Sys->ARM9Target;

        return;
    }

    // step the pipeline.
    ARM_PipelineStep(cpu);

    if (cpu->CPSR.Thumb)
    {
        const struct ARM_Instr instr = cpu->Instr[0];
        const u16 decode = (instr.Thumb >> 10);

        ILCheck(16, THUMB9_InterlockLUT[decode])
        FetchIRQExec(16, THUMB9_InstructionLUT[decode](cpu, instr))
    }
    else
    {
        const struct ARM_Instr instr = cpu->Instr[0];
        const u8 condcode = instr.Arm >> 28;
        const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

        // TODO: DATA ABORTS?????
        // first we need to check the condition code (should be part of decoding?)
        if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
        {
            ILCheck(32, ARM9_InterlockLUT[decode])
            FetchIRQExec(32, ARM9_InstructionLUT[decode](cpu, instr))
        }
        else if (condcode == ARMCond_NV) // unconditional instructions
        {
            ILCheck(32, ARM9_Uncond_Interlocks)
            FetchIRQExec(32, ARM9_Uncond(cpu, instr))
        }
        else if (decode == 0x127) // BKPT; needs special handling, condition code is ignored (always passes)
        {
            // bkpt doesn't use registers and can't interlock.
            FetchIRQExec(32, ARM9_PrefetchAbort(cpu, instr))
        }
        else // actually an instruction that failed the condition check.
        {
            // CHECKME: skipped instructions shouldn't trigger interlocks right?
            ARM9_InstrRead32(ARM9, cpu->PC);

            // this needs a special check because im stupid and reusing this path for pipeline refills
            if (cpu->Instr[0].Flushed || !ARM9_CheckInterrupts(ARM9))
            {
                ARM9_ExecuteCycles(ARM9, 1, 1);
                ARM_StepPC(cpu, false);
            }
        }
    }
}

#undef ILCheck
#undef FetchIRQExecute

void ARM9_MainLoop(struct ARM946ES* ARM9)
{
    while(true)
    {
        if (cpu->Timestamp > cpu->Sys->ARM9Target)
            CR_Switch(cpu->Sys->HandleMain);
        else
            ARM9_Step(ARM9);
    }
}

#undef cpu
