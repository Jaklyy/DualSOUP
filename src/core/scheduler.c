#include "scheduler.h"
#include "console.h"
#include "utils.h"
#include <stdckdint.h>




void Scheduler_UpdateTargets(struct Console* sys)
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

    sys->Sched.EventCallbacks[nextevt](sys, sys->Sched.EventTimes[nextevt]);
    Scheduler_UpdateTargets(sys);
}

void Scheduler_RunEventManual(struct Console* sys, timestamp time, const u8 event, const u8 a9)
{
    // need to catch up other components to ensure coherency
    if (a9 && (time >= Console_GetARM7Cur(sys)))
        CR_Switch(sys->HandleARM7);
    if (!a9 && (time >= Console_GetARM9Cur(sys)))
        CR_Switch(sys->HandleARM9);

    if (time >= sys->Sched.EventTimes[event])
    {
        sys->Sched.EventCallbacks[event](sys, sys->Sched.EventTimes[event]);
        Scheduler_UpdateTargets(sys);
    }
}

void Schedule_Event(struct Console* sys, void (*callback) (struct Console*, timestamp), u8 event, timestamp time)
{
    sys->Sched.EventTimes[event] = time;
    sys->Sched.EventCallbacks[event] = callback;
    Scheduler_UpdateTargets(sys);
}
