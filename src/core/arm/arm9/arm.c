#include "../../utils.h"
#include "../shared/arm.h"
#include "../shared/instr.h"
#include "instr_il.h"
#include "arm.h"




#define cpu ((struct ARM*)ARM9)

// TEMP: debugging
void ARM9_Log(struct ARM946ES* ARM9)
{
    LogPrint(LOG_ARM9, "DUMPING ARM9 STATE:\n");
    /*for (int i = 0; i < 16; i++)
    {
        LogPrint(LOG_ARM9, "R%2i: %08X ", i, cpu->R[i]);
    }*/
    LogPrint(LOG_ARM9, "R2:%08X\n", cpu->R[2]);
    LogPrint(LOG_ARM9, "CPSR:%08X\n", cpu->CPSR.Raw);
    LogPrint(LOG_ARM9, "INSTR: %08X ", cpu->Instr[0]);
    LogPrint(LOG_ARM9, "EXE:%li MEM:%li\n\n", cpu->Timestamp, ARM9->MemTimestamp);
}

void ARM9_Init(struct ARM946ES* ARM9, struct Console* sys)
{
    ARM_Init(cpu, sys, ARM9ID);

    ARM9->CP15.CR.FixedOnes = 0xF;

    // finally something being initialized that isn't a constant!
    // this needs to not be 0, because a lot of logic relies on this timestamp - 1
    //ARM9->MemTimestamp = 1; nvm we dont need this actually i was dumb
}

union ARM_PSR ARM9_GetSPSR(struct ARM946ES* ARM9)
{
    switch(cpu->CPSR.Mode)
    {
    case ARMMODE_FIQ:
        return cpu->FIQ_Bank.SPSR;
    case ARMMODE_IRQ:
        return cpu->IRQ_Bank.SPSR;
    case ARMMODE_SWI:
        return cpu->SWI_Bank.SPSR;
    case ARMMODE_SWI+1 ... ARMMODE_ABT:
        return cpu->ABT_Bank.SPSR;
    case ARMMODE_ABT+1 ... ARMMODE_UND:
        return cpu->UND_Bank.SPSR;
    case ARMMODE_USR:
    case ARMMODE_UND+1 ... ARMMODE_SYS:
        return cpu->CPSR;
    default: unreachable();
    }
}

void ARM9_SetSPSR(struct ARM946ES* ARM9, union ARM_PSR psr)
{
    switch(cpu->CPSR.Mode)
    {
    case ARMMODE_FIQ:
        cpu->FIQ_Bank.SPSR = psr;
        break;
    case ARMMODE_IRQ:
        cpu->IRQ_Bank.SPSR = psr;
        break;
    case ARMMODE_SWI:
        cpu->SWI_Bank.SPSR = psr;
        break;
    case ARMMODE_SWI+1 ... ARMMODE_ABT:
        cpu->ABT_Bank.SPSR = psr;
        break;
    case ARMMODE_ABT+1 ... ARMMODE_UND:
        cpu->UND_Bank.SPSR = psr;
        break;
    case ARMMODE_USR:
    case ARMMODE_UND+1 ... ARMMODE_SYS:
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
    // arm9 enforces pc alignment properly for once.
    addr &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3);
    // r15 interlocks must be resolved immediately.
    ARM9_InterlockStall(ARM9, iloffs);

    // pipeline flush logic goes here.
    // TODO: handle stalls if icache streaming is ongoing here?
    // TEMP?
    if (cpu->CPSR.Thumb)
    {
        cpu->Instr[1] = ARM9_InstrRead16(ARM9, addr);
        cpu->Instr[2] = ARM9_InstrRead16(ARM9, addr+2);
        cpu->PC = addr + 4;
    }
    else
    {
        cpu->Instr[1] = ARM9_InstrRead32(ARM9, addr+2);
        cpu->Instr[2] = ARM9_InstrRead32(ARM9, addr+4);
        cpu->PC = addr + 8;
    }
    // "fake" execute stage to keep timestamps coherent.
    // TODO: do this differently it confuses me.
    ARM9_ExecuteCycles(ARM9, 1, 1);
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

    // make sure we're caught up to the memory timestamp
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
}

[[nodiscard]] bool ARM9_CheckInterrupts(struct ARM946ES* ARM9)
{
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
            ARM9_InterruptRequest(ARM9);
            return true;
        }
    }
    else return false;
}

#define ILCheck(x) \
s8 stall = x (ARM9, instr); \
if (stall) \
{ \
    ARM9_InterlockStall(ARM9, stall); \
}

#define FetchIRQExec(size, x) \
cpu->Instr[2] = ARM9_InstrRead##size (ARM9, cpu->PC); \
if (!ARM9_CheckInterrupts(ARM9)) \
    x \
    (cpu, instr);

void ARM9_Step(struct ARM946ES* ARM9)
{
    // step pipeline forward.
    cpu->Instr[0] = cpu->Instr[1];
    cpu->Instr[1] = cpu->Instr[2];

    if (cpu->CPSR.Thumb)
    {
        u16 instr = cpu->Instr[0];
        u16 decode = (instr >> 10);

        ILCheck(THUMB9_InterlockLUT[decode])
        FetchIRQExec(16, THUMB9_InstructionLUT[decode])
    }
    else
    {
        u32 instr = cpu->Instr[0];
        u8 condcode = instr >> 28;
        u16 decode = ((instr >> 16) & 0xFF0) | ((instr >> 4) & 0xF);

        // TODO: DATA ABORTS?????
        // first we need to check the condition code (should be part of decoding?)
        if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
        {
            // partial decode stage modelling in order to check for and handle interlocks.
            // must be done before fetch stage for proper cycle accuracy.
            ILCheck(ARM9_InterlockLUT[decode])
            FetchIRQExec(32, ARM9_InstructionLUT[decode])
        }
        else if (condcode == ARMCOND_NV) // unconditional instructions
        {
            ILCheck(ARM9_Uncond_Interlocks)
            FetchIRQExec(32, ARM9_Uncond)
        }
        else if (decode == 0x127) // BKPT; needs special handling, it always passes condition codes
        {
            // bkpt doesn't use registers and can't interlock.
            FetchIRQExec(32, ARM9_PrefetchAbort)
        }
        else // actually an instruction that failed the condition check.
        {
            // CHECKME: skipped instructions shouldn't trigger interlocks right?

            cpu->Instr[2] = ARM9_InstrRead32(ARM9, cpu->PC);

            if (!ARM9_CheckInterrupts(ARM9))
                ARM9_ExecuteCycles(ARM9, 1, 1);
        }
    }

    // TEMP: debugging
    ARM9_Log(ARM9);
}

#undef ILCheck
#undef FetchIRQExec

#undef cpu
