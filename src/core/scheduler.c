#include "scheduler.h"
#include "console.h"
#include "utils.h"
#include <stdckdint.h>


inline timestamp NTRClock_CvtFrom16(timestamp ts)
{
    return ts * (Sched_Clock / NTR_BaseClock);
}

inline timestamp NTRClock_CvtFrom33(timestamp ts)
{
    return ts * (Sched_Clock / NTR_SysClock);
}

inline timestamp NTRClock_CvtFrom67(timestamp ts)
{
    return ts * (Sched_Clock / NTR9_Clock);
}

inline timestamp NTRClock_67Align33(timestamp ts)
{
    constexpr timestamp adjust = (NTR9_Clock / NTR_SysClock)-1;
    return NTRClock_CvtFrom67((ts + adjust) & ~adjust);
}


void NeoSched_RemoveEvent(NeoSched* sched, Scheduler_Events id)
{
    if (sched->Prev[id] != Evt_Invalid) // event was scheduled, unsechedule it
    {
        sched->Next[sched->Prev[id]] = sched->Next[id];
        sched->Prev[sched->Next[id]] = sched->Prev[id];

        sched->Next[id] = Evt_Invalid;
        sched->Prev[id] = Evt_Invalid;
    }
}

timestamp NeoSched_GetTime(NeoSched* sched, Scheduler_Events id)
{
    if (sched->Prev[id] != Evt_Invalid) // event is scheduled
    {
        return sched->Times[id];
    }
    else
    {
        return timestamp_max; // idk
    }
}

bool NeoSched_CheckEventScheduled(Console* sys, Scheduler_Events id)
{
    return sys->Sched.Prev[id] != Evt_Invalid;
}

void NeoSched_AddEvent(Console* sys, timestamp time, Scheduler_Events id)
{
    NeoSched* sched = &sys->Sched;
    Scheduler_Events prev = Evt_Null;
    Scheduler_Events next = Evt_Null;

    NeoSched_RemoveEvent(sched, id);

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

void NeoSched_AddEventIfEarlier(Console* sys, timestamp time, Scheduler_Events id)
{
    if (time < NeoSched_GetTime(&sys->Sched, id))
    {
        NeoSched_AddEvent(sys, time, id);
    }
}

void NeoSched_RunEvent(Console* sys)
{
    NeoSched* sched = &sys->Sched;
    Scheduler_Events evt = sched->Next[Evt_Null];
    timestamp now = sched->Times[evt];

    NeoSched_RemoveEvent(sched, evt);

    switch(evt)
    {
    case Evt_Null:
    case Evt_Max:
    case Evt_Invalid:
    //default:
        CrashSpectacularly("FATAL: INVALID SCHEDULER EVENT: %"PRIu8"\n", evt);

    case Evt_ARM9:      A946_MainLoop(&sys->ARM9); break;
    case Evt_ARM9BIU:   AHB9_BusRun(sys, now); break;
    case Evt_ARM7:      ARM7_MainLoop(&sys->ARM7); break;
    case Evt_MainRAM:   MainRAM_Run(sys, now); break;
    }
}

#if 0
void Scheduler_UpdateTargets(Console* sys)
{
    timestamp next = timestamp_max;
    for (int i = 0; i < Evt_Max; i++)
    {
        if (next > sys->Sched.EventTimes[i])
        {
            next = sys->Sched.EventTimes[i];
        }
    }
    sys->MainTarget = next;
}

void Scheduler_Run(Console* sys)
{
#ifdef REALTHREAD
    mtx_lock(&sys->Sched.SchedulerMtx);
#endif

    u8 nextevt = Evt_Max;
    if (sys->MainTarget == timestamp_max) CrashSpectacularly("FATAL: INVALID SCHEDULER TARGET\n");

    for (int i = 0; i < Evt_Max; i++)
    {
        if (sys->MainTarget >= sys->Sched.EventTimes[i])
        {
            nextevt = i;
            break;
        }
    }
    if (nextevt == Evt_Max) CrashSpectacularly("WHAT\n");

    sys->Sched.EventCallbacks[nextevt](sys, sys->Sched.EventTimes[nextevt]);
    Scheduler_UpdateTargets(sys);

#ifdef REALTHREAD
    mtx_unlock(&sys->Sched.SchedulerMtx);
#endif
}

void Schedule_Event(Console* sys, void (*callback) (Console*, timestamp), u8 event, timestamp time)
{
#ifdef REALTHREAD
    mtx_lock(&sys->Sched.SchedulerMtx);
#endif

    sys->Sched.EventTimes[event] = time;
    sys->Sched.EventCallbacks[event] = callback;
    Scheduler_UpdateTargets(sys);

#ifdef REALTHREAD
    mtx_unlock(&sys->Sched.SchedulerMtx);
#endif
}

#define A9GO ((sys->A9Sync < sys->MainTarget) && (sys->MR9 ? (sys->A9Sync < sys->A7Sync) : (sys->A9Sync <= sys->A7Sync)))
#define A7GO ((sys->A7Sync < sys->MainTarget) && ((sys->MR7 && !sys->ExtMemCR_Shared.MRPriority) ? (sys->A7Sync < sys->A9Sync) : (sys->A7Sync <= sys->A9Sync)))
#define SYSGO ((sys->A9Sync >= sys->MainTarget) && (sys->A7Sync >= sys->MainTarget))
// if this isn't always inlined the compiler wont optimize out the SyncMode stuff properly.
forceinline void Scheduler_Sync(Console* sys, timestamp now, const SyncMode mode)
{
    if (mode >= Sync_9)
    {
        sys->A9Sync = now;
        if (mode == Sync_MainRAM9) sys->MR9 = true;
        if (mode == Sync_Sleep9) sys->Sleep9 = true;
    }
    else
    {
        sys->A7Sync = now;
        if (mode & Sync_MainRAM7) sys->MR7 = true;
        if (mode & Sync_Sleep7) sys->Sleep7 = true;
    }

    while (true)
    {
        if A9GO
        {
            if (mode < Sync_9) CR_Switch(sys->HandleARM9);
            else break;
        }
        else if A7GO
        {
            if (mode >= Sync_9) CR_Switch(sys->HandleARM7);
            else break;
        }
        while SYSGO
        {
            Scheduler_Run(sys);
        }
    }

    if (mode >= Sync_9)
    {
        if (mode == Sync_MainRAM9) sys->MR9 = false;
        if (mode == Sync_Sleep9) sys->Sleep9 = false;
    }
    else
    {
        if (mode == Sync_MainRAM7) sys->MR7 = false;
        if (mode == Sync_Sleep7) sys->Sleep7 = false;
    }
}
#undef A9GO
#undef A7GO
#undef SYSGO

void Scheduler_StallForEvent(Console* sys, timestamp* time, const u8 event, const bool a9)
{
    // make sure the event is actually scheduled
    if (sys->Sched.EventTimes[event] == timestamp_max) return;

    // wait until event time
    DS_CLAMP(*time, <, sys->Sched.EventTimes[event])

    Scheduler_Sync(sys, *time, (a9 ? Sync_Normal9 : Sync_Normal7));
}
#endif
