#pragma once
#include "utils.h"




typedef struct Console Console;

typedef enum : u8
{
    Evt_Null,

    Evt_DebugBreak,
    Evt_PollInput,

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
    Evt_Timer90CR,
    Evt_Timer91CR,
    Evt_Timer92CR,
    Evt_Timer93CR,
    Evt_Timer90Run,
    Evt_Timer91Run,
    Evt_Timer92Run,
    Evt_Timer93Run,
    Evt_Bus9Cmp,
    Evt_Bus9Arb,
    Evt_Divider,
    Evt_Sqrt,
    //Evt_GXExec,
    Evt_GX,

    Evt_UpdateIRQ7,

    Evt_ARM7,
    Evt_ARM7DataRead,
    Evt_ARM7InstrRead,
    Evt_ARM7DataWrite,
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
    Evt_TimerSnd0CR,
    Evt_TimerSnd1CR,
    Evt_TimerSnd2CR,
    Evt_TimerSnd3CR,
    Evt_TimerSnd4CR,
    Evt_TimerSnd5CR,
    Evt_TimerSnd6CR,
    Evt_TimerSnd7CR,
    Evt_TimerSnd8CR,
    Evt_TimerSnd9CR,
    Evt_TimerSndACR,
    Evt_TimerSndBCR,
    Evt_TimerSndCCR,
    Evt_TimerSndDCR,
    Evt_TimerSndECR,
    Evt_TimerSndFCR,
    Evt_TimerSnd0Run,
    Evt_TimerSnd1Run,
    Evt_TimerSnd2Run,
    Evt_TimerSnd3Run,
    Evt_TimerSnd4Run,
    Evt_TimerSnd5Run,
    Evt_TimerSnd6Run,
    Evt_TimerSnd7Run,
    Evt_TimerSnd8Run,
    Evt_TimerSnd9Run,
    Evt_TimerSndARun,
    Evt_TimerSndBRun,
    Evt_TimerSndCRun,
    Evt_TimerSndDRun,
    Evt_TimerSndERun,
    Evt_TimerSndFRun,
    Evt_Timer70CR,
    Evt_Timer71CR,
    Evt_Timer72CR,
    Evt_Timer73CR,
    Evt_Timer70Run,
    Evt_Timer71Run,
    Evt_Timer72Run,
    Evt_Timer73Run,
    Evt_Bus7Cmp,
    Evt_Bus7Arb,
    Evt_SPI,
    Evt_MixAudio,

    Evt_IO9,
    Evt_IO7,
    Evt_MainRAM,

    Evt_Scanline,
    Evt_HBlank,
    Evt_CardROM,
    Evt_CardSPI9,
    Evt_CardSPI7,

    Evt_ConsolePowerOff,
    Evt_EndFrame,

    Evt_Invalid,

    Evt_Max
} Scheduler_Events;

static_assert(Evt_PollInput < Evt_IO9 && Evt_PollInput < Evt_IO7, "POLL EVENT RESOLVES BEFORE IO EVENTS\n");

typedef struct
{
    timestamp Times[Evt_Max];
    Scheduler_Events Next[Evt_Max];
    Scheduler_Events Prev[Evt_Max];
} Sched;

typedef enum : u8
{
    Core_Continue,
    Core_EndFrame,
    Core_Break,
    Core_Poll,
    Core_PowerOff,
} Core_Ret;

// clock conversion helpers

 // convert 16 mhz clock to standardized scheduler clock
timestamp DSClk16(timestamp ts);
 // convert 33 mhz clock to standardized scheduler clock
timestamp DSClk33(timestamp ts);
 // convert 67 mhz clock to standardized scheduler clock
timestamp DSClk67(timestamp ts);

 // align standardized scheduler clock with 33 mhz clock
timestamp DSClkAlign33(timestamp ts);


void Sched_Log(Console* sys);
void Sched_RemoveEvent(Sched* sched, Scheduler_Events id);
timestamp Sched_GetTime(Sched* sched, Scheduler_Events id);
bool Sched_CheckEventScheduled(Console* sys, Scheduler_Events id);
void Sched_AddEvent(Console* sys, timestamp time, Scheduler_Events id);
void Sched_AddEventIfEarlier(Console* sys, timestamp time, Scheduler_Events id);
Core_Ret Sched_RunEvent(Console* sys);

void DS_BREAK(Console* sys);
