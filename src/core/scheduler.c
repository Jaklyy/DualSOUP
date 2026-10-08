#include "scheduler.h"
#include "console.h"
#include "core/arm/arm9/arm.h"
#include "core/bus/bus.h"
#include "core/io/timer.h"
#include "core/irq.h"
#include "core/video/3d.h"
#include "core/video/video.h"
#include "utils.h"
#include <stdckdint.h>


void DS_BREAK(Console* sys)
{
    Sched_AddEvent(sys, 0, Evt_DebugBreak);
}

inline timestamp DSClk16(timestamp ts)
{
    return ts * (Sched_Clock / NTR_BaseClock);
}

inline timestamp DSClk33(timestamp ts)
{
    return ts * (Sched_Clock / NTR_SysClock);
}

inline timestamp DSClk67(timestamp ts)
{
    return ts * (Sched_Clock / NTR9_Clock);
}

inline timestamp DSClkAlign33(timestamp ts)
{
    constexpr timestamp adjust = (Sched_Clock / NTR_SysClock)-1;
    return (ts + adjust) & ~adjust;
}

void Sched_Dump(Console* sys)
{
    for (int i = 0; i < Evt_Max; i++)
    {
        if ((i % 3) == 0) printf("\n");
        printf("%02i: %02u %02u %016lX  ", i, sys->Sched.Next[i], sys->Sched.Prev[i], sys->Sched.Times[i]);
    }
    printf("\n");
}

void Sched_Log(Console* sys)
{
    //Sched_Dump(sys);
#if 1
    Sched* sched = &sys->Sched;
    Scheduler_Events evt = Evt_Null;
    printf("sched dump:\n");
    do
    {
        evt = sched->Next[evt];
        printf("%02i: %016lX\n", evt, sched->Times[evt]);
    } while(sched->Next[evt] != Evt_Invalid);
#endif
}

void Sched_RemoveEvent(Sched* sched, Scheduler_Events id)
{
    if (sched->Prev[id] != Evt_Invalid) // event was scheduled, unsechedule it
    {
        Scheduler_Events previd = sched->Prev[id];
        Scheduler_Events nextid = sched->Next[id];
        sched->Next[previd] = nextid;
        sched->Prev[nextid] = previd;

        sched->Next[id] = Evt_Invalid;
        sched->Prev[id] = Evt_Invalid;
    }
}

timestamp Sched_GetTime(Sched* sched, Scheduler_Events id)
{
    if (sched->Prev[id] != Evt_Invalid) return sched->Times[id]; // event is scheduled
    else return timestamp_max; // idk
}

bool Sched_CheckEventScheduled(Console* sys, Scheduler_Events id)
{
    return sys->Sched.Prev[id] != Evt_Invalid;
}

void Sched_AddEvent(Console* sys, timestamp time, Scheduler_Events id)
{
    Sched* sched = &sys->Sched;
    Scheduler_Events prev = Evt_Null;
    Scheduler_Events next = Evt_Null;

    Sched_RemoveEvent(sched, id);

    do
    {
        prev = next;
        next = sched->Next[next];
    }
    while ((time > sched->Times[next]) // find next and previous events
        || ((time == sched->Times[next]) && (id > next /* determine priority? */))); // break ties

    // insert event into adjacent
    sched->Prev[next] = id;
    sched->Next[prev] = id;

    sched->Prev[id] = prev;
    sched->Next[id] = next;
    sched->Times[id] = time;
}

void Sched_AddEventIfEarlier(Console* sys, timestamp time, Scheduler_Events id)
{
    if (time < Sched_GetTime(&sys->Sched, id)) Sched_AddEvent(sys, time, id);
}

Core_Ret Sched_RunEvent(Console* sys)
{
    Sched* sched = &sys->Sched;
    Scheduler_Events evt = sched->Next[Evt_Null];
    timestamp now = sched->Times[evt];

    Sched_RemoveEvent(sched, evt);

    switch(evt)
    {
    case Evt_Null:
    case Evt_Max:
    case Evt_Invalid:
    default:
        CrashSpectacularly("FATAL: INVALID SCHEDULER EVENT: %"PRIu8"\n", evt);

    case Evt_IRQ9_VBlank
     ... Evt_IRQ9_Time3:    IF9_Set(sys, (evt - Evt_IRQ9_VBlank + IRQ_VBlank), now); break;
    case Evt_IRQ9_DMA0
     ... Evt_IRQ9_AGBPak:   IF9_Set(sys, (evt - Evt_IRQ9_DMA0 + IRQ_DMA0), now); break;
    case Evt_IRQ9_IPCSync
     ... Evt_IRQ9_GXFIFO:   IF9_Set(sys, (evt - Evt_IRQ9_IPCSync + IRQ_IPCSync), now); break;

    case Evt_IRQ7_VBlank
     ... Evt_IRQ7_AGBPak:   IF7_Set(sys, (evt - Evt_IRQ7_VBlank + IRQ_VBlank), now); break;
    case Evt_IRQ7_IPCSync
     ... Evt_IRQ7_NTRCard:  IF7_Set(sys, (evt - Evt_IRQ7_IPCSync + IRQ_IPCSync), now); break;
    case Evt_IRQ7_Lid
     ... Evt_IRQ7_WiFi:     IF7_Set(sys, (evt - Evt_IRQ7_Lid + IRQ_LidOpen), now); break;

    case Evt_UpdateIRQ9:    IRQ9_Update(sys, now); break;
    case Evt_ARM9:          A946_Run(&sys->A946ES, now); break;
    case Evt_ARM9WBFill:    A946_WriteBufferFillRun(&sys->A946ES, now); break;
    case Evt_ARM9BIU:       A946_BIURun(&sys->A946ES, now); break;

    case Evt_DMA90
     ... Evt_DMA93:         DMA_Step(sys, evt-Evt_DMA90, now, true); break;
    case Evt_Timer90CR
     ... Evt_Timer93CR:     Timer_UpdateCR(sys, sys->Timers9, evt-Evt_Timer90CR, now, TimerType_9); break;
    case Evt_Timer90Run
     ... Evt_Timer93Run:    Timer_Run(sys, sys->Timers9, evt-Evt_Timer90Run, now, TimerType_9); break;

    case Evt_Bus9Cmp:       Bus_RunCmp(sys, now, true); break;
    case Evt_Bus9Arb:       Bus_RunArb(sys, now, true); break;
    case Evt_Divider:       IO9_FinishDiv(sys); break;
    case Evt_Sqrt:          IO9_FinishSqrt(sys); break;

    case Evt_GX:            GX_RunFIFO(sys, now); break;

    case Evt_UpdateIRQ7:    IRQ7_Update(sys, now); break;
    case Evt_ARM7:          A7TDMI_Run(&sys->A7TDMI, now); break;
    case Evt_ARM7DataRead:  A7TDMI_DataReadActual(&sys->A7TDMI, now); break;
    case Evt_ARM7InstrRead: A7TDMI_InstrReadActual(&sys->A7TDMI, now); break;
    case Evt_ARM7DataWrite: A7TDMI_DataWriteActual(&sys->A7TDMI, now); break;

    case Evt_SCapDMA70
     ... Evt_DMA73:         DMA_Step(sys, evt-Evt_SCapDMA70, now, false); break;

    case Evt_TimerSnd0CR
     ... Evt_TimerSndFCR:   Timer_UpdateCR(sys, sys->TimersSound, evt-Evt_TimerSnd0CR, now, TimerType_Snd); break;
    case Evt_TimerSnd0Run
     ... Evt_TimerSndFRun:  Timer_Run(sys, sys->TimersSound, evt-Evt_TimerSnd0Run, now, TimerType_Snd); break;

    case Evt_Timer70CR
     ... Evt_Timer73CR:     Timer_UpdateCR(sys, sys->Timers7, evt-Evt_Timer70CR, now, TimerType_7); break;
    case Evt_Timer70Run
     ... Evt_Timer73Run:    Timer_Run(sys, sys->Timers7, evt-Evt_Timer70Run, now, TimerType_7); break;

    case Evt_Bus7Cmp:       Bus_RunCmp(sys, now, false); break;
    case Evt_Bus7Arb:       Bus_RunArb(sys, now, false); break;

    case Evt_SPI:           SPI_Finish(sys, now); break;
    case Evt_MixAudio:      AudioMixer_Sample(sys, now); break;

    case Evt_IO9:           IO9_Handler(sys, now); break;
    case Evt_IO7:           IO7_Handler(sys, now); break;
    case Evt_MainRAM:       MainRAM_Run(sys, now); break;

    case Evt_Scanline:      LCD_Scanline(sys, now); break;
    case Evt_HBlank:        LCD_HBlank(sys, now); break;
    case Evt_CardROM:       GameCard_HandleSchedulingROM(sys, now); break;
    case Evt_CardSPI9:      GameCard_SPIFinish(sys, true); break;
    case Evt_CardSPI7:      GameCard_SPIFinish(sys, false); break;

    case Evt_ConsolePowerOff: sys->NewSync = now; return Core_PowerOff;
    case Evt_EndFrame: sys->NewSync = now; return Core_EndFrame;
    case Evt_PollInput: sys->NewSync = now; return Core_Poll;
    case Evt_DebugBreak: sys->NewSync = now; return Core_Break;
    }
    return Core_Continue;
}
