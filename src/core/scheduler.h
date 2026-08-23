#pragma once
#ifdef REALTHREAD
    #include <threads.h>
#endif
#include "utils.h"




typedef struct Console Console;

typedef enum : u8
{
    Evt_Null,

    Evt_ARM9,
    Evt_ARM9BIU,
    Evt_DMA90,
    Evt_DMA91,
    Evt_DMA92,
    Evt_DMA93,
    Evt_AHB9,
    Evt_IF9Update,
    Evt_Divider,
    Evt_Sqrt,
    Evt_Timer9,
    //Evt_GXExec,
    Evt_GX,

    Evt_ARM7,
    Evt_DMA70,
    Evt_DMA71,
    Evt_DMA72,
    Evt_DMA73,
    Evt_Bus7,
    Evt_IF7Update,
    Evt_Timer7,
    Evt_SPI,
    Evt_MixAudio,

    Evt_IO9,
    Evt_IO7,
    Evt_MainRAM,

    Evt_Scanline,
    Evt_CardROM,
    Evt_CardSPI,

    Evt_Invalid,

    Evt_Max
} Scheduler_Events;

typedef struct
{
    timestamp Times[Evt_Max];
    Scheduler_Events Next[Evt_Max];
    Scheduler_Events Prev[Evt_Max];
} NeoSched;

typedef struct
{
    alignas(HOST_CACHEALIGN) timestamp EventTimes[Evt_Max];
    void (*EventCallbacks[Evt_Max]) (Console*, timestamp);

#ifdef REALTHREAD
    mtx_t SchedulerMtx;
#endif
} OldSched;

typedef union
{
    NeoSched Neo;
    OldSched Old;
} Scheduler;

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


// clock conversion helpers

 // convert 16 mhz clock to standardized scheduler clock
inline timestamp NTRClock_CvtFrom16(timestamp ts);
 // convert 33 mhz clock to standardized scheduler clock
inline timestamp NTRClock_CvtFrom33(timestamp ts);
 // convert 67 mhz clock to standardized scheduler clock
inline timestamp NTRClock_CvtFrom67(timestamp ts);

 // convert 67 mhz clock to standardized scheduler clock, while aligning clock with 33 mhz clock
inline timestamp NTRClock_67Align33(timestamp ts);


void NeoSched_RemoveEvent(NeoSched* sched, Scheduler_Events id);
bool NeoSched_CheckEventScheduled(Console* sys, Scheduler_Events id);
void NeoSched_AddEvent(Console* sys, timestamp time, Scheduler_Events id);
void NeoSched_AddEventIfEarlier(Console* sys, timestamp time, Scheduler_Events id);
void NeoSched_RunEvent(Console* sys);

// schedule an event to run
void Schedule_Event(Console* sys, void (*callback) (Console*, timestamp), u8 event, timestamp time);
// sync arm9, arm7, and system.
void Scheduler_Sync(Console* sys, timestamp now, const SyncMode mode);
// stall until an event is run
void Scheduler_StallForEvent(Console* sys, timestamp* time, const u8 event, const bool a9);
