#include "scheduler.h"
#include "console.h"
#include "core/arm/arm9/arm.h"
#include "core/bus/bus.h"
#include "core/io/timer.h"
#include "utils.h"
#include <stdckdint.h>


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


void Sched_RemoveEvent(Sched* sched, Scheduler_Events id)
{
    if (sched->Prev[id] != Evt_Invalid) // event was scheduled, unsechedule it
    {
        sched->Next[sched->Prev[id]] = sched->Next[id];
        sched->Prev[sched->Next[id]] = sched->Prev[id];

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

    while ((time < sched->Times[sched->Next[next]]) // find next and previous events
        || ((time == sched->Times[sched->Next[next]]) && (id > sched->Next[next] /* determine priority? */))) // break ties
    {
        prev = next;
        next = sched->Next[next];
    }

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

void Sched_RunEvent(Console* sys)
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
    //default:
        CrashSpectacularly("FATAL: INVALID SCHEDULER EVENT: %"PRIu8"\n", evt);

    case Evt_IRQ9_VBlank
     ... Evt_IRQ9_Time3:    IF9_Set(sys, (evt - Evt_IRQ9_VBlank), now); break;
    case Evt_IRQ9_DMA0
     ... Evt_IRQ9_AGBPak:   IF9_Set(sys, (evt - Evt_IRQ9_DMA0), now); break;
    case Evt_IRQ9_IPCSync
     ... Evt_IRQ9_GXFIFO:   IF9_Set(sys, (evt - Evt_IRQ9_IPCSync), now); break;

    case Evt_IRQ7_VBlank
     ... Evt_IRQ7_AGBPak:   IF7_Set(sys, (evt - Evt_IRQ7_VBlank), now); break;
    case Evt_IRQ7_IPCSync
     ... Evt_IRQ7_NTRCard:  IF7_Set(sys, (evt - Evt_IRQ7_IPCSync), now); break;
    case Evt_IRQ7_Lid
     ... Evt_IRQ7_WiFi:     IF7_Set(sys, (evt - Evt_IRQ7_Lid), now); break;

    case Evt_UpdateIRQ9:    IRQ9_Update(sys, now); break;
    case Evt_ARM9:          A946_Run(&sys->A946ES); break;
    case Evt_ARM9WBFill:    A946_WriteBufferFillRun(&sys->A946ES, now); break;
    case Evt_ARM9BIU:       A946_BIURun(&sys->A946ES, now); break;

    case Evt_DMA90
     ... Evt_DMA93:         DMA_Step(sys, evt-Evt_DMA90, now, false); break;
    case Evt_Timer9:        (sys->timertemp9 == TIMER_UPDATECR) ? Timer9_UpdateCRs(sys, now) : Timer_SchedRun9(sys, now); break;

    case Evt_Bus9HReady:    Bus_TransferPost(sys, now, true); break;
    case Evt_Bus9:          Bus_Run(sys, now, true); break;
    case Evt_Divider:       IO9_FinishDiv(sys); break;
    case Evt_Sqrt:          IO9_FinishSqrt(sys); break;

    //case Evt_GX:

    case Evt_UpdateIRQ7:    IRQ7_Update(sys, now); break;
    case Evt_ARM7:          A7TDMI_Run(&sys->A7TDMI, now); break;

    case Evt_SCapDMA70
     ... Evt_DMA73:         DMA_Step(sys, evt-Evt_SCapDMA70, now, false); break;
    case Evt_Timer7:        (sys->timertemp7 == TIMER_UPDATECR) ? Timer7_UpdateCRs(sys, now) : Timer_SchedRun7(sys, now); break;

    case Evt_Bus7HReady:    Bus_TransferPost(sys, now, false); break;
    case Evt_Bus7:          Bus_Run(sys, now, false); break;

    case Evt_SPI:           SPI_Finish(sys, now); break;
    //case Evt_MixAudio:

    case Evt_IO9:           IO9_Handler(sys, now); break;
    case Evt_IO7:           IO7_Handler(sys, now); break;
    case Evt_MainRAM:       MainRAM_Run(sys, now); break;

    //case Evt_Scanline:
    case Evt_CardROM:       GameCard_HandleSchedulingROM(sys, now); break;
    case Evt_CardSPI9:      GameCard_SPIFinish(sys, true); break;
    case Evt_CardSPI7:      GameCard_SPIFinish(sys, false); break;

    case Evt_HaltCore:      sys->CoreRunning = false; break;
    }
}
