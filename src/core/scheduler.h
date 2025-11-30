#pragma once
#include "utils.h"




struct Console;

enum Scheduler_Events : u8
{
    Sched_DMA9,
    Sched_IF9Update,
    Sched_Divider,

    Sched_DMA7,
    Sched_IF7Update,

    Sched_Scanline,

    Sched_MAX
};

struct Scheduler
{
    alignas(HOST_CACHEALIGN) timestamp EventTimes[Sched_MAX];
    void (*EventCallbacks[Sched_MAX]) (struct Console*, timestamp);
};

// update targets
void Scheduler_UpdateTargets(struct Console* sys);
// run the next event in the scheduler
void Scheduler_Run(struct Console* sys);
// check to run an event manually
void Scheduler_RunEventManual(struct Console* sys, timestamp time, const u8 event, const u8 a9);
// schedule an event to run
void Schedule_Event(struct Console* sys, void (*callback) (struct Console*, timestamp), u8 event, timestamp time);
