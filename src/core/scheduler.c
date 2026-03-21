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

// TODO: replacing all of the while loops with if statements should be a bit faster, but it isn't a safe optimization currently.
// i suspect this might be due to swaps for scheduling? this might be a place for future optimizations.

void Scheduler_SyncWith7GTE(struct Console* sys, timestamp now)
{
    sys->A9Sync = now;

    while(now >= sys->A7Sync)
        CR_Switch(sys->HandleMain);

    Scheduler_TryRun(sys, true, now);
}

void Scheduler_SyncWith7GT(struct Console* sys, timestamp now)
{
    sys->A9Sync = now;

    while(now > sys->A7Sync)
        CR_Switch(sys->HandleMain);

    Scheduler_TryRun(sys, true, now);
}

void Scheduler_SyncWith9MR(struct Console* sys, timestamp now)
{
    sys->A7Sync = now;

    while(((!sys->ExtMemCR_Shared.MRPriority) ? (now >= sys->A9Sync) : (now > sys->A9Sync)))
        CR_Switch(sys->HandleMain);

    Scheduler_TryRun(sys, false, now);
}

void Scheduler_SyncWith9GT(struct Console* sys, timestamp now)
{
    sys->A7Sync = now;

    while(now > sys->A9Sync)
        CR_Switch(sys->HandleMain);

    Scheduler_TryRun(sys, false, now);
}

void Scheduler_TryRun(struct Console* sys, const bool a9, const timestamp now)
{
    if (a9) sys->A9Sync = now;
    else    sys->A7Sync = now;

    while(now >= sys->MainTarget)
        CR_Switch(sys->HandleMain);
}

void Scheduler_StallToRunEvent(struct Console* sys, timestamp* time, const u8 event, const u8 a9)
{
    // make sure the event is actually scheduled
    if (sys->Sched.EventTimes[event] == timestamp_max) return;

    // wait until event time
    DS_CLAMP(*time, <, sys->Sched.EventTimes[event])

    timestamp* sync = (a9) ? &sys->A9Sync : &sys->A7Sync;
    *sync = *time;

    while(*sync >= sys->Sched.EventTimes[event])
        CR_Switch(sys->HandleMain);
}
