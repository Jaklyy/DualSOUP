#pragma once
#include <threads.h>
#include "utils.h"




struct Console;

enum Scheduler_Events : u8
{
    //Evt_DMA9,
    //Evt_DMA7,
    Evt_IF9Update,
    Evt_Divider,
    Evt_Sqrt,
    Evt_Timer9,
    //Evt_GXExec,
    Evt_GX,

    Evt_IF7Update,
    Evt_Timer7,
    Evt_SPI,
    Evt_MixAudio,

    Evt_Scanline,
    Evt_CardROM,
    Evt_CardSPI,

    Evt_Max
};

struct Scheduler
{
    alignas(HOST_CACHEALIGN) timestamp EventTimes[Evt_Max];
    void (*EventCallbacks[Evt_Max]) (struct Console*, timestamp);

    mtx_t SchedulerMtx;
};

void Scheduler_SyncWith7GTE(struct Console* sys, timestamp now);
void Scheduler_SyncWith7GT(struct Console* sys, timestamp now);
void Scheduler_SyncWith9MR(struct Console* sys, timestamp now);
void Scheduler_SyncWith9GT(struct Console* sys, timestamp now);
// update targets
void Scheduler_UpdateTargets(struct Console* sys);
// run the next event in the scheduler
void Scheduler_Run(struct Console* sys);
// try to run any and all events if possible.
void Scheduler_TryRun(struct Console* sys, const bool a9, const timestamp now);
// stall until an event is run
void Scheduler_StallToRunEvent(struct Console* sys, timestamp* time, const u8 event, const u8 a9);
// schedule an event to run
void Schedule_Event(struct Console* sys, void (*callback) (struct Console*, timestamp), u8 event, timestamp time);
