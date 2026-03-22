#include "scheduler.h"
#include "console.h"
#include "utils.h"
#include <stdckdint.h>
#include <threads.h>




void Scheduler_UpdateTargets(struct Console* sys)
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

void Scheduler_Run(struct Console* sys)
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

void Schedule_Event(struct Console* sys, void (*callback) (struct Console*, timestamp), u8 event, timestamp time)
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
__attribute((always_inline)) void Scheduler_Sync(struct Console* sys, timestamp now, const SyncMode mode)
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

    if A9GO
    {
        if (mode < Sync_9) CR_Switch(sys->HandleARM9);
    }
    else if A7GO
    {
        if (mode >= Sync_9) CR_Switch(sys->HandleARM7);
    }
    else while SYSGO
    {
        Scheduler_Run(sys);
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

void Scheduler_StallForEvent(struct Console* sys, timestamp* time, const u8 event, const bool a9)
{
    // make sure the event is actually scheduled
    if (sys->Sched.EventTimes[event] == timestamp_max) return;

    // wait until event time
    DS_CLAMP(*time, <, sys->Sched.EventTimes[event])

    Scheduler_Sync(sys, *time, (a9 ? Sync_Normal9 : Sync_Normal7));
}
