#pragma once
#include "utils.h"




enum Scheduler_Events : u8
{
    Sched_DMA9,
    Sched_Timer9IRQ,

    Sched_DMA7,

    Sched_MAX,
};

struct Scheduler
{
    alignas(HOST_CACHEALIGN) timestamp EventTimes[Sched_MAX];
};

struct Console;

void Scheduler_Check(struct Console* sys);
void Schedule_Event(struct Console* sys, u8 event, timestamp time);
