#pragma once
#include "utils.h"




struct Console;

enum Scheduler_Events : u8
{
    Sched_DMA9,
    Sched_Timer9IRQ,

    Sched_DMA7,

    Sched_Scanline,

    Sched_MAX,
};

struct Scheduler
{
    alignas(HOST_CACHEALIGN) timestamp EventTimes[Sched_MAX];
    void (*EventCallbacks[Sched_MAX]) (struct Console*);
};

void Scheduler_Check(struct Console* sys);
void Scheduler_Run(struct Console* sys);
void Schedule_Event(struct Console* sys, void (*callback) (struct Console*), u8 event, timestamp time, const bool offset);
