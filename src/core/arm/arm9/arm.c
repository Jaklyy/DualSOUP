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
    LogPrint(LOG_ARM9, "INSTR: %08X ", cpu->Instr[0].Raw);
    LogPrint(LOG_ARM9, "DTCM: %08lX %08lX %i ITCM: %i %i %i\n", ARM9->CP15.DTCMReadBase, ARM9->CP15.DTCMWriteBase, ARM9->CP15.DTCMShift, ARM9->CP15.ITCMShift, ARM9->CP15.CR.ITCMEnable, ARM9->CP15.CR.ITCMLoadMode);
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

    // set as no interlocks
    ARM9->RegIL.Raw = -1;

    // TODO: initial values are placeholders
    ARM9->CP15.DCachePRNG = 0x0123456789ABCDEF;
    ARM9->CP15.ICachePRNG = 0xFEDCBA9876543210;
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

#if 0
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
#if 1
    if (stall > 0)
    {
        cpu->Timestamp = ARM9->MemTimestamp + stall - 1;
        ARM9_UpdateInterlocks(ARM9, stall);
    }
#else
    // branchless version, seems to be significantly slower.
    u64 mask = (((s64)stall - 1) >> 63);
    cpu->Timestamp += ((ARM9->MemTimestamp + ((s64)stall - 1)) - cpu->Timestamp) & ~mask;
    ARM9_UpdateInterlocks(ARM9, stall & ~mask);
#endif
}
#endif
void ARM9_SetPC(struct ARM946ES* ARM9, u32 addr)
{
    // arm9 enforces pc alignment properly in arm mode.
    addr &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3);

    // assign potential interlocks here
    //ARM9->RegIL[15][0] = iloffs;

    cpu->PC = addr;
    cpu->Prog = ARMProg_RefillStart;
}

void ARM9_SetReg(struct ARM946ES* ARM9, const int reg, u32 val)
{
    if (reg == 15) // PC must be handled specially
    {
        ARM9_SetPC(ARM9, val); // CHECKME: should this be the port C time?
    }
    else
    {
        // I pray that nothing makes it any more complex than this.
        cpu->R[reg] = val;
        //ARM9->RegIL[reg][0] = iloffs;
        //ARM9->RegIL[reg][1] = iloffs_c;
    }
}

#if 0
void ARM9_CheckInterlocks(struct ARM946ES* ARM9, s8* stall, const int reg, const s8 cycledelay, const bool portc)
{
    // the fact this always needs a branch really annoys me.
    // but i dont think this is possible to work around without losing accuracy.
#if 1
    s8 diff = ARM9->RegIL[reg][portc] - cycledelay;
    if (*stall < diff) *stall = diff;
#else
    // TODO: this *can* be done branchless according to a friend; but it needs profiling.
    s8 diff = ARM9->RegIL[reg][portc] - cycledelay;

    s8 mask = (*stall - diff);
    *stall = (mask & ~(mask>>7)) + diff;
#endif
}
#endif
void ARM9_FetchCycles(struct ARM946ES* ARM9, const int fetch)
{
    cpu->Timestamp += fetch;

    // next instruction cannot execute until the last memory stage is complete
    if (cpu->Timestamp < (ARM9->MemTimestamp))
        cpu->Timestamp = (ARM9->MemTimestamp);
}

void ARM9_ExecuteCycles(struct ARM946ES* ARM9, const int execute)
{
    cpu->Timestamp += execute - 1;
#if 0
    // execute cycles must be minus 1 due to how im handling pipeline overlaps
    cpu->Timestamp += execute - 1;

    // catch the memory timestamp up
    // save the difference between old and new so we can also catch up the interlock timestamps
    s64 diff = (cpu->Timestamp + memory) - ARM9->MemTimestamp;
    ARM9->MemTimestamp += diff;
    if (diff > s8_max) diff = s8_max;
    if (diff < s8_min) diff = s8_min;

    ARM9_UpdateInterlocks(ARM9, diff);

    cpu->CodeSeq = true;
#endif
}

void ARM9_FixupLoadStore(struct ARM946ES* ARM9, const int execute, s64 memdiff)
{
    cpu->Timestamp += execute - 1;
    if (memdiff > s8_max) memdiff = s8_max;
    if (memdiff < s8_min) memdiff = s8_min;
    ARM9_UpdateInterlocks(ARM9, memdiff);
}

[[nodiscard]] bool ARM9_CheckInterrupts(struct ARM946ES* ARM9)
{
    Scheduler_Sync(cpu->Sys, cpu->Timestamp >> A9ClockShift(*ARM9), Sync_Normal9);

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

void ARM9_DeferredITCMWrite(struct ARM946ES* ARM9);

// interlocks are stalled for before fetching
// note: this is probably be checked for on the prior decode stage, but i dont think that matters?
s8 ARM9_DecodeInterlocks(struct ARM946ES* ARM9, const bool thumb, const s8 reg, const s8 len, const s8 len_c)
{
    s8 stall;
    if (thumb)
    {
        const ARM_Instr instr = cpu->Instr[1];
        const u16 decode = (instr.Thumb >> 10);
        stall = THUMB9_InterlockLUT[decode](ARM9, instr, reg, len, len_c);
        //ARM9_InterlockStall(ARM9, stall);
    }
    else
    {
        const ARM_Instr instr = cpu->Instr[1];
        const u8 condcode = instr.Arm >> 28;
        const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

        // decode instructions
        if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
        {
            stall = ARM9_InterlockLUT[decode](ARM9, instr, reg, len, len_c);
            //ARM9_InterlockStall(ARM9, stall);
        }
        else if (condcode == ARMCond_NV)
        {
            stall = ARM9_Uncond_Interlocks(ARM9, instr, reg, len, len_c);
            //ARM9_InterlockStall(ARM9, stall);
        }
        else
        {
            // checkme: i dont think failed condcode instructions stall for interlocks.
            stall = 0;
        }
    }
    return stall;
}

bool ARM9_Fetch(struct ARM946ES* ARM9)
{
    // begin instruction fetch
    bool ret = ARM9_InstrRead(ARM9);

    // TODO: itcm data reads may have to be done here?
    ARM9_DeferredITCMWrite(ARM9);
    return ret;
}

void ARM9_Exec(struct ARM946ES* ARM9)
{
    if (!ARM9_CheckInterrupts(ARM9))
    {
        if (cpu->CPSR.Thumb)
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u16 decode = (instr.Thumb >> 10);

            THUMB9_InstructionLUT[decode](cpu, instr);
        }
        else
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u8 condcode = instr.Arm >> 28;
            const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

            // TODO: DATA ABORTS?????
            // first we need to check the condition code (should be part of decoding?)
            if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
            {
                ARM9_InstructionLUT[decode](cpu, instr);
            }
            else if (condcode == ARMCond_NV) // unconditional instructions
            {
                ARM9_Uncond(cpu, instr);
            }
            else if (decode == 0x127) // BKPT; needs special handling, condition code is ignored (always passes)
            {
                // bkpt doesn't use registers and can't interlock.
                ARM9_PrefetchAbort(cpu, instr);
            }
            else // actually an instruction that failed the condition check.
            {
                ARM9_ExecuteCycles(ARM9, 1);
                ARM_StepPC(cpu, false);
            }
        }
    }
}

#undef ILCheck
#undef FetchIRQExecute

void ARM9_MainLoop(struct ARM946ES* ARM9)
{
    if (biuactive)
    {

    }

    // largely represents the arm9e-s core
    switch()
    {
    case execute:
    {
        // run next instruction
    }
    case instronly:
    {
        // fetch instr
        // somehow handle branches here seamlessly?
        // test interlocks in next instruction? (probably not correct to do this here...?)
    }
    case dataonly:
    {
        // do data fetch
        // run callback
    }
    case instrthendata:
    {
        // fetch instr
        // somehow handle branches here seamlessly?
        // test interlocks in next instruction? (probably not correct to do this here...?)
        // do data fetch
        // run callback
    }
    case datatheninstr:
    {
        // fetch instr
        // somehow handle branches here seamlessly?
        // test interlocks in next instruction? (probably not correct to do this here...?)
        // do data fetch
        // run callback
    }
    default: // blocked by biu
    {

    }
    }
#if 0
    if (cpu->Prog & instr)
    {

    }
    else if (& data)
    {

    }
    else // instr??
    {

    }
#endif
#if 0
    switch(cpu->Prog)
    {
        case ARMProg_Sleep:
        {
            // TODO?
            /*if (cpu->CpuSleeping)
            {
                if (Console_CheckARM9Wake(cpu->Sys))
                {
                    cpu->CpuSleeping = 0;
                }
                else
                {
                    // probably slow; but ensures write buffer drains properly.
                    if (ARM9->WBuffer.FIFOFillPtr != 16)
                    {
                        ARM9_CatchUpWriteBuffer(ARM9, &cpu->Timestamp);
                        ARM9_ExecuteCycles(ARM9, 2, 1);
                        return;
                    }
                    else
                    {
                        cpu->DeadAsleep = true;
                        return;
                    }
                }
            }

            ARM9_CatchUpWriteBuffer(ARM9, &cpu->Timestamp);*/
            //if (!Console_CheckARM9Wake(cpu->Sys))
            {
                return;
            }
            //cpu->Prog = ARMProg_Fetch;
            //[[fallthrough]]; // checkme?
        }
        case ARMProg_RefillStart:
        {
            {
                // TEMP: debugging
                //ARM9_DumpMPU(ARM9);
                //ARM9_Log(ARM9);
                //if (cpu->PC == 0x020C42A8) CrashSpectacularly("OSPANIC\n");
            }
            // non-sequential
            cpu->CodeSeq = false;
            // handle nonsequential accesses updating the instruction region.
            ARM9_UpdateInstrRegion(ARM9);
            [[fallthrough]];
        }
        case ARMProg_RefillMid:
        {
            ARM_PipelineStep(cpu);
            // dont handle interlocks here because that's silly (waiting for this to somehow bite me in the ass)
            cpu->Prog += 1 + ARM9_Fetch(ARM9);
            static_assert(((ARMProg_RefillStart     + 1) == ARMProg_RefillStartBusy)
                       && ((ARMProg_RefillStartBusy + 1) == ARMProg_RefillMid)
                       && ((ARMProg_RefillMid       + 1) == ARMProg_RefillMidBusy)
                       && ((ARMProg_RefillMidBusy   + 1) == ARMProg_Fetch), "ARM PROG NEEDS ADJUSTING HERE");
            break;
        }
        case ARMProg_Fetch:
        {
            ARM_PipelineStep(cpu);
            //ARM9_DecodeInterlocks(ARM9);
            cpu->Prog += 1 + ARM9_Fetch(ARM9);
            static_assert(((ARMProg_Fetch + 1) == ARMProg_FetchBusy), "ARM PROG NEEDS ADJUSTING HERE");
            break;
        }
        case ARMProg_Exec:
        {
            ARM9_Exec(ARM9);
            static_assert(false, "PROG????\n");
            break;
        }
        case ARMProg_SleepCheck:
        {
            break;
        }
    }
#endif
#if 0
    ARM9_FlushPipeline(ARM9);
    while(!cpu->Sys->KillThread)
    {
        if (!cpu->DeadAsleep)
        {
            if ((cpu->Timestamp >> A9ClockShift(*ARM9)) < cpu->Sys->MainTarget)
            {
                if (DMA_GetNext(cpu->Sys, 0, false, true) <= (cpu->Timestamp >> A9ClockShift(*ARM9)))
                {
                    DMA_Run(cpu->Sys, true);
                }
                else
                {
                    ARM9_Step(ARM9);
                }
            }
            else
            {
                Scheduler_Sync(cpu->Sys, cpu->Timestamp >> A9ClockShift(*ARM9), Sync_Normal9);
            }
        }
        else
        {
            if (DMA_GetNext(cpu->Sys, 0, false, true) < cpu->Sys->MainTarget)
            {
                DMA_Run(cpu->Sys, true);
            }
            else
            {
                Scheduler_Sync(cpu->Sys, DMA_GetNext(cpu->Sys, 0, false, true), Sync_Sleep9);
            }
        }
    }
#endif
}

#undef cpu
