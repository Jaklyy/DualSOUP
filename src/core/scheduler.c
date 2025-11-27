#include "scheduler.h"
#include "console.h"
#include "utils.h"
#include <stdckdint.h>




void Scheduler_Check(struct Console* sys)
{
    timestamp next = timestamp_max;
    for (int i = 0; i < Sched_MAX; i++)
    {
        if (next > sys->Sched.EventTimes[i])
            next = sys->Sched.EventTimes[i];
    }
    if (ckd_mul(&sys->ARM9Target, next, 2))
        sys->ARM9Target = timestamp_max;

    sys->ARM7Target = next;
}

void Scheduler_Run(struct Console* sys)
{
    u8 nextevt = Sched_MAX;
    if (sys->ARM7Target == timestamp_max) return;

    for (int i = 0; i < Sched_MAX; i++)
    {
        if (sys->ARM7Target >= sys->Sched.EventTimes[i])
        {
            nextevt = i;
            break;
        }
    }
    if (nextevt == Sched_MAX) CrashSpectacularly("WHAT\n");

    sys->Sched.EventCallbacks[nextevt](sys);
    Scheduler_Check(sys);
}

void Schedule_Event(struct Console* sys, void (*callback) (struct Console*), u8 event, timestamp time, const bool offset)
{
    if (offset)
    {
        sys->Sched.EventTimes[event] += time;
    }
    else
    {
        sys->Sched.EventTimes[event] = time;
    }
    sys->Sched.EventCallbacks[event] = callback;
    Scheduler_Check(sys);
}
