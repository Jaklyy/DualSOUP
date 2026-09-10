#pragma once
#ifdef REALTHREAD
    #include <threads.h>
#endif
#include "utils.h"




typedef struct Console Console;

typedef enum : u8
{
    Evt_Null,

    Evt_IRQ9_VBlank,
    Evt_IRQ9_HBlank,
    Evt_IRQ9_VCount,
    Evt_IRQ9_Time0,
    Evt_IRQ9_Time1,
    Evt_IRQ9_Time2,
    Evt_IRQ9_Time3,
    Evt_IRQ9_DMA0,
    Evt_IRQ9_DMA1,
    Evt_IRQ9_DMA2,
    Evt_IRQ9_DMA3,
    Evt_IRQ9_Keypad,
    Evt_IRQ9_AGBPak,
    Evt_IRQ9_IPCSync,
    Evt_IRQ9_IPCFIFOEmpty,
    Evt_IRQ9_IPCFIFONotEmpty,
    Evt_IRQ9_NTRCardTranferComplete,
    Evt_IRQ9_NTRCard,
    Evt_IRQ9_GXFIFO,

    Evt_IRQ7_VBlank,
    Evt_IRQ7_HBlank,
    Evt_IRQ7_VCount,
    Evt_IRQ7_Time0,
    Evt_IRQ7_Time1,
    Evt_IRQ7_Time2,
    Evt_IRQ7_Time3,
    Evt_IRQ7_SIO,
    Evt_IRQ7_DMA0,
    Evt_IRQ7_DMA1,
    Evt_IRQ7_DMA2,
    Evt_IRQ7_DMA3,
    Evt_IRQ7_Keypad,
    Evt_IRQ7_AGBPak,
    Evt_IRQ7_IPCSync,
    Evt_IRQ7_IPCFIFOEmpty,
    Evt_IRQ7_IPCFIFONotEmpty,
    Evt_IRQ7_NTRCardTranferComplete,
    Evt_IRQ7_NTRCard,
    Evt_IRQ7_Lid,
    Evt_IRQ7_SPI,
    Evt_IRQ7_WiFi,

    Evt_UpdateIRQ9,

    Evt_ARM9,
    Evt_ARM9WBFill,
    Evt_ARM9BIU,
    Evt_DMA90,
    Evt_DMA91,
    Evt_DMA92,
    Evt_DMA93,
    Evt_Timer9,
    Evt_Bus9HReady,
    Evt_Bus9,
    Evt_Divider,
    Evt_Sqrt,
    //Evt_GXExec,
    Evt_GX,

    Evt_UpdateIRQ7,

    Evt_ARM7,
    Evt_SCapDMA70,
    Evt_SCapDMA71,
    Evt_SndDMA70,
    Evt_SndDMA71,
    Evt_SndDMA72,
    Evt_SndDMA73,
    Evt_SndDMA74,
    Evt_SndDMA75,
    Evt_SndDMA76,
    Evt_SndDMA77,
    Evt_SndDMA78,
    Evt_SndDMA79,
    Evt_SndDMA7A,
    Evt_SndDMA7B,
    Evt_SndDMA7C,
    Evt_SndDMA7D,
    Evt_SndDMA7E,
    Evt_SndDMA7F,
    Evt_DMA70,
    Evt_DMA71,
    Evt_DMA72,
    Evt_DMA73,
    Evt_Timer7,
    Evt_Bus7HReady,
    Evt_Bus7,
    Evt_SPI,
    Evt_MixAudio,

    Evt_IO9,
    Evt_IO7,
    Evt_MainRAM,

    Evt_Scanline,
    Evt_CardROM,
    Evt_CardSPI9,
    Evt_CardSPI7,

    Evt_HaltCore,

    Evt_Invalid,

    Evt_Max
} Scheduler_Events;

typedef struct
{
    timestamp Times[Evt_Max];
    Scheduler_Events Next[Evt_Max];
    Scheduler_Events Prev[Evt_Max];
} Sched;

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
    Sched Neo;
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
timestamp DSClk16(timestamp ts);
 // convert 33 mhz clock to standardized scheduler clock
timestamp DSClk33(timestamp ts);
 // convert 67 mhz clock to standardized scheduler clock
timestamp DSClk67(timestamp ts);

 // align standardized scheduler clock with 33 mhz clock
timestamp DSClkAlign33(timestamp ts);


void Sched_RemoveEvent(Sched* sched, Scheduler_Events id);
timestamp Sched_GetTime(Sched* sched, Scheduler_Events id);
bool Sched_CheckEventScheduled(Console* sys, Scheduler_Events id);
void Sched_AddEvent(Console* sys, timestamp time, Scheduler_Events id);
void Sched_AddEventIfEarlier(Console* sys, timestamp time, Scheduler_Events id);
void Sched_RunEvent(Console* sys);
