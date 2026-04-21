#pragma once
#ifdef REALTHREAD
    #include <threads.h>
#endif
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

#ifdef REALTHREAD
    mtx_t SchedulerMtx;
#endif
};

typedef enum : u8
{
    Sync_7 = 0x00,
    Sync_Normal7 = 0x00,
    Sync_MainRAM7 = 0x01,
    Sync_Sleep7 = 0x02,

    Sync_9 = 0x80,
    Sync_Normal9 = 0x80,
    Sync_MainRAM9 = 0x81,
    Sync_Sleep9 = 0x82,
} SyncMode;

// schedule an event to run
void Schedule_Event(struct Console* sys, void (*callback) (struct Console*, timestamp), u8 event, timestamp time);
// sync arm9, arm7, and system.
void Scheduler_Sync(struct Console* sys, timestamp now, const SyncMode mode);
// stall until an event is run
void Scheduler_StallForEvent(struct Console* sys, timestamp* time, const u8 event, const bool a9);
