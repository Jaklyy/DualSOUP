#include "scheduler.h"
#include "console.h"
#include <stdckdint.h>




void Scheduler_Check(struct Console* sys)
{
    timestamp next = timestamp_max;
    for (int i = 0; i < Sched_MAX; i++)
    {
        if (next < sys->Sched.EventTimes[i])
            next = sys->Sched.EventTimes[i];
    }
    //if (ckd_mul(&sys->ARM9Target, next, 2))
        sys->ARM9Target = timestamp_max;

    sys->ARM7Target = next;
}

void Schedule_Event(struct Console* sys, u8 event, timestamp time)
{
    sys->Sched.EventTimes[Sched_DMA9] = time;
    Scheduler_Check(sys);
}
