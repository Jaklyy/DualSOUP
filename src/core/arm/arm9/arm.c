#include "../../utils.h"
#include "../shared/arm.h"
#include "../../console.h"
#include "instr_il.h"
#include "arm.h"




#define cpu ((ARM*)a946)

// TEMP: debugging
void A946_Log(ARM946ES* a946)
{
    LogPrint(LOG_ARM9, "DUMPING ARM9 STATE:\n");
    for (int i = 0; i < 16; i++)
    {
        LogPrint(LOG_ARM9, "R%2i: %08X ", i, cpu->R[i]);
    }
    //LogPrint(LOG_ARM9, "R2:%08X\n", cpu->R[2]);
    LogPrint(LOG_ARM9, "CPSR:%08X\n", cpu->CPSR.Raw);
    LogPrint(LOG_ARM9, "INSTR: %08X ", cpu->Instr[0].Raw);
    LogPrint(LOG_ARM9, "DTCM: %08lX %08lX %i ITCM: %i %i %i\n", a946->CP15.DTCMReadBase, a946->CP15.DTCMWriteBase, a946->CP15.DTCMShift, a946->CP15.ITCMShift, a946->CP15.CR.ITCMEnable, a946->CP15.CR.ITCMLoadMode);
    //LogPrint(LOG_ARM9, "EXE:%li MEM:%li\n\n", cpu->Timestamp, a946->MemTimestamp);
}

void A946_Init(ARM946ES* a946, Console* sys)
{
    ARM_Init(cpu, sys, ARM9ID);

    // set permanently set CP15 CR bits
    a946->CP15.CR.FixedOnes = 0xF;

    // set as no interlocks
    a946->RegIL.Raw = -1;

    // TODO: initial values are placeholders
    a946->CP15.DCachePRNG = 0x0123456789ABCDEF;
    a946->CP15.ICachePRNG = 0xFEDCBA9876543210;
}

ARM_PSR A9ES_GetSPSR(ARM946ES* a946)
{
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

void A9ES_SetSPSR(ARM946ES* a946, ARM_PSR psr)
{
    switch(cpu->CPSR.Mode)
    {
    case ARMMode_FIQ:                   cpu->FIQ_Bank.SPSR = psr; break;
    case ARMMode_IRQ:                   cpu->IRQ_Bank.SPSR = psr; break;
    case ARMMode_SVC:                   cpu->SVC_Bank.SPSR = psr; break;
    case ARMMode_SVC+1 ... ARMMode_ABT: cpu->ABT_Bank.SPSR = psr; break;
    case ARMMode_ABT+1 ... ARMMode_UND: cpu->UND_Bank.SPSR = psr; break;
    case ARMMode_USR:
    case ARMMode_UND+1 ... ARMMode_SYS: break; // no spsr, no write
    default: unreachable();
    }
    return;
}

u32 A9ES_GetReg(ARM946ES* a946, const int reg)
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
void A9ES_UpdateInterlocks(ARM946ES* a946, const s8 diff)
{
    // i spent some time writing simd for this manually but it's so simple auto-simd was just as good.
    for (int i = 0; i < 32; i++)
    {
        a946->RegIL[i & 0xF][i>>4] -= diff;
        if (a946->RegIL[i & 0xF][i>>4] < 0) a946->RegIL[i & 0xF][i>>4] = 0;
    }
}
void A9ES_InterlockStall(ARM946ES* a946, const s8 stall)
{
#if 1
    if (stall > 0)
    {
        cpu->Timestamp = a946->MemTimestamp + stall - 1;
        A9ES_UpdateInterlocks(a946, stall);
    }
#else
    // branchless version, seems to be significantly slower.
    u64 mask = (((s64)stall - 1) >> 63);
    cpu->Timestamp += ((a946->MemTimestamp + ((s64)stall - 1)) - cpu->Timestamp) & ~mask;
    A9ES_UpdateInterlocks(a946, stall & ~mask);
#endif
}
#endif
void A9ES_SetPC(ARM946ES* a946, u32 addr)
{
    // arm9 enforces pc alignment properly in arm mode.
    addr &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3);

    // assign potential interlocks here
    //a946->RegIL[15][0] = iloffs;

    cpu->PC = addr;
    cpu->Prog = ARMProg_RefillStart;
}

void A9ES_SetReg(ARM946ES* a946, const int reg, u32 val)
{
    if (reg == 15) // PC must be handled specially
    {
        A9ES_SetPC(a946, val); // CHECKME: should this be the port C time?
    }
    else
    {
        // I pray that nothing makes it any more complex than this.
        cpu->R[reg] = val;
        //a946->RegIL[reg][0] = iloffs;
        //a946->RegIL[reg][1] = iloffs_c;
    }
}

#if 0
void A9ES_CheckInterlocks(ARM946ES* a946, s8* stall, const int reg, const s8 cycledelay, const bool portc)
{
    // the fact this always needs a branch really annoys me.
    // but i dont think this is possible to work around without losing accuracy.
#if 1
    s8 diff = a946->RegIL[reg][portc] - cycledelay;
    if (*stall < diff) *stall = diff;
#else
    // TODO: this *can* be done branchless according to a friend; but it needs profiling.
    s8 diff = a946->RegIL[reg][portc] - cycledelay;

    s8 mask = (*stall - diff);
    *stall = (mask & ~(mask>>7)) + diff;
#endif
}
#endif

void A9ES_ExecuteCycles(ARM946ES* a946, const int execute)
{
    cpu->Timestamp += execute - 1;

    if (execute > 1)  a946->RegIL.Raw = -1; // clear interlocks
#if 0
    // execute cycles must be minus 1 due to how im handling pipeline overlaps
    cpu->Timestamp += execute - 1;

    // catch the memory timestamp up
    // save the difference between old and new so we can also catch up the interlock timestamps
    s64 diff = (cpu->Timestamp + memory) - a946->MemTimestamp;
    a946->MemTimestamp += diff;
    if (diff > s8_max) diff = s8_max;
    if (diff < s8_min) diff = s8_min;

    A9ES_UpdateInterlocks(a946, diff);

    cpu->CodeSeq = true;
#endif
}

#if 0
void A9ES_FixupLoadStore(ARM946ES* a946, const int execute, s64 memdiff)
{
    cpu->Timestamp += execute - 1;
    if (memdiff > s8_max) memdiff = s8_max;
    if (memdiff < s8_min) memdiff = s8_min;
    A9ES_UpdateInterlocks(a946, memdiff);
}
#endif

[[nodiscard]] bool A9ES_CheckInterrupts(ARM946ES* a946)
{
    //Scheduler_Sync(cpu->Sys, cpu->Timestamp >> A9ClockShift(*a946), Sync_Normal9);

    // todo: schedule this instead
    if (cpu->Sys->IME9 && !cpu->CPSR.IRQDisable && (cpu->Sys->IE9 & cpu->Sys->IF9))
    {
#if 0
        if (cpu->FastInterruptRequest) // jakly why are you implementing this...
        {
            A9ES_FastInterruptRequest(a946);
            return true;
        }
        else
#endif
        {
            A9ES_InterruptRequest(a946);
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

// interlocks are stalled for before fetching
// note: this is probably be checked for on the prior decode stage, but i dont think that matters?
s8 A9ES_DecodeInterlocks(ARM946ES* a946, const bool thumb, const s8 reg, const s8 len, const s8 len_c)
{
    s8 stall;
    if (thumb)
    {
        const ARM_Instr instr = cpu->Instr[1];
        const u16 decode = (instr.Thumb >> 10);
        stall = T9ES_InterlockLUT[decode](a946, instr, reg, len, len_c);
        //A9ES_InterlockStall(a946, stall);
    }
    else
    {
        const ARM_Instr instr = cpu->Instr[1];
        const u8 condcode = instr.Arm >> 28;
        const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

        // decode instructions
        if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
        {
            stall = A9ES_InterlockLUT[decode](a946, instr, reg, len, len_c);
            //A9ES_InterlockStall(a946, stall);
        }
        else if (condcode == ARMCond_NV)
        {
            stall = A9ES_Uncond_Interlocks(a946, instr, reg, len, len_c);
            //A9ES_InterlockStall(a946, stall);
        }
        else
        {
            // checkme: i dont think failed condcode instructions stall for interlocks.
            stall = 0;
        }
    }
    return stall;
}

inline void ARM9ES_SetTwoCycleInterlock(ARM946ES* a946, const u8 reg)
{
    a946->RegIL.Next = reg | 0x80;
}

void ARM9ES_TestTwoCycleInterlocks(ARM946ES* a946)
{
    if (!(a946->RegIL.Cur & 0x10)) // reg is interlocked
    {
        if (A9ES_DecodeInterlocks(a946, cpu->CPSR.Thumb, a946->RegIL.Cur & 0xF, 1, 1))
        {
            static_assert(false, "idk what to do here\n");
        }
    }

    a946->RegIL.Raw >>= 8; // step interlock tracker
}

bool A9ES_Fetch(ARM946ES* a946)
{
    // begin instruction fetch
    bool ret = A9ES_InstrRead(a946);

    // TODO: itcm data reads may have to be done here?
    //A9ES_DeferredITCMWrite(a946);
    return ret;
}

void A9ES_Exec(ARM946ES* a9es)
{
    if (!A9ES_CheckInterrupts(a9es))
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
                A9ES_InstructionLUT[decode](cpu, instr);
            }
            else if (condcode == ARMCond_NV) // unconditional instructions
            {
                A9ES_Uncond(cpu, instr);
            }
            else if (decode == 0x127) // BKPT; needs special handling, condition code is ignored (always passes)
            {
                // bkpt doesn't use registers and can't interlock.
                A9ES_PrefetchAbort(cpu, instr);
            }
            else // actually an instruction that failed the condition check.
            {
                A9ES_ExecuteCycles(a946, 1);
                ARM_StepPC(cpu, false);
            }
        }
    }
}

void A946_AddMemCycles(ARM946ES* a946)
{
    if ((a946->IBus <= A946_BusDone) && (a946->DBus <= A946_BusDone))
    {
        timestamp next = a946->ARM.Timestamp;
        if (a946->IBus == A946_BusDone) DS_CLAMP(next, <, a946->InstrTS)
        if (a946->DBus == A946_BusDone) DS_CLAMP(next, <, a946->DataTS)
    }
}

void A946_MainLoop(ARM946ES* a946, timestamp now)
{
    if ((a946->IBus == A946_BusGo) || (a946->DBus == A946_BusGo))
    {
        while ((a946->IBus == A946_BusGo) || (a946->DBus == A946_BusGo))
        {
            if ((a946->ITCMMultiplexData || (a946->IBus == A946_BusNone)) && (a946->DBus == A946_BusGo))
            {
                // do data load first if the itcm multiplexer is set to data
            }
            else if (a946->IBus == A946_BusGo)
            {
                // otherwise do instruction load first
                A946_InstrRead(a946, now);
            }
        }
    }
    else
    {
        // execute next instruction if no accesses are waiting
        A9ES_Exec(a946);
    }
#if 0
    {
    case execute:
    {
        // run next instruction
    }
    case instronly:
    {
        // test interlocks in next instruction? (probably not correct to do this here...?)
        ARM9ES_TestTwoCycleInterlocks(a946);
        // fetch instr
        A9ES_Fetch()
        // somehow handle branches here seamlessly?
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
    }
#endif
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
                    if (a946->WBuffer.FIFOFillPtr != 16)
                    {
                        A9ES_CatchUpWriteBuffer(a946, &cpu->Timestamp);
                        A9ES_ExecuteCycles(a946, 2, 1);
                        return;
                    }
                    else
                    {
                        cpu->DeadAsleep = true;
                        return;
                    }
                }
            }

            A9ES_CatchUpWriteBuffer(a946, &cpu->Timestamp);*/
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
                //A9ES_DumpMPU(a946);
                //A9ES_Log(a946);
                //if (cpu->PC == 0x020C42A8) CrashSpectacularly("OSPANIC\n");
            }
            // non-sequential
            cpu->CodeSeq = false;
            // handle nonsequential accesses updating the instruction region.
            A9ES_UpdateInstrRegion(a946);
            [[fallthrough]];
        }
        case ARMProg_RefillMid:
        {
            ARM_PipelineStep(cpu);
            // dont handle interlocks here because that's silly (waiting for this to somehow bite me in the ass)
            cpu->Prog += 1 + A9ES_Fetch(a946);
            static_assert(((ARMProg_RefillStart     + 1) == ARMProg_RefillStartBusy)
                       && ((ARMProg_RefillStartBusy + 1) == ARMProg_RefillMid)
                       && ((ARMProg_RefillMid       + 1) == ARMProg_RefillMidBusy)
                       && ((ARMProg_RefillMidBusy   + 1) == ARMProg_Fetch), "ARM PROG NEEDS ADJUSTING HERE");
            break;
        }
        case ARMProg_Fetch:
        {
            ARM_PipelineStep(cpu);
            //A9ES_DecodeInterlocks(a946);
            cpu->Prog += 1 + A9ES_Fetch(a946);
            static_assert(((ARMProg_Fetch + 1) == ARMProg_FetchBusy), "ARM PROG NEEDS ADJUSTING HERE");
            break;
        }
        case ARMProg_Exec:
        {
            A9ES_Exec(a946);
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
    A9ES_FlushPipeline(a946);
    while(!cpu->Sys->KillThread)
    {
        if (!cpu->DeadAsleep)
        {
            if ((cpu->Timestamp >> A9ClockShift(*a946)) < cpu->Sys->MainTarget)
            {
                if (DMA_GetNext(cpu->Sys, 0, false, true) <= (cpu->Timestamp >> A9ClockShift(*a946)))
                {
                    DMA_Run(cpu->Sys, true);
                }
                else
                {
                    A9ES_Step(a946);
                }
            }
            else
            {
                Scheduler_Sync(cpu->Sys, cpu->Timestamp >> A9ClockShift(*a946), Sync_Normal9);
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
