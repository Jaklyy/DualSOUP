#pragma once
#include <threads.h>
#include "utils.h"




struct Console;

enum Scheduler_Events : u8
{
    Evt_DMA9,
    Evt_DMA7,
    Evt_IF9Update,
    Evt_Divider,
    Evt_Sqrt,
    Evt_Timer9,

    Evt_IF7Update,
    Evt_Timer7,
    Evt_SPI,

    Evt_Scanline,
    Evt_Gamecard,

    Evt_Max
};

struct Scheduler
{
    alignas(HOST_CACHEALIGN) timestamp EventTimes[Evt_Max];
    void (*EventCallbacks[Evt_Max]) (struct Console*, timestamp);

    mtx_t SchedulerMtx;
};

// update targets
void Scheduler_UpdateTargets(struct Console* sys);
// run the next event in the scheduler
void Scheduler_Run(struct Console* sys);
// check to run an event manually
void Scheduler_RunEventManual(struct Console* sys, timestamp time, const u8 event, const u8 a9);
// schedule an event to run
void Schedule_Event(struct Console* sys, void (*callback) (struct Console*, timestamp), u8 event, timestamp time);
