#pragma once
#include "utils.h"




typedef struct Console Console;

#define ENUM_NAMES \
ENUMPH(Null) \
 \
ENUMPH(DebugBreak) \
ENUMPH(PollInput) \
 \
ENUMPH(UpdateIRQ9) \
ENUMPH(UpdateIRQ7) \
 \
ENUMPH(IRQ9_VBlank) ENUMPH(IRQ9_HBlank) ENUMPH(IRQ9_VCount) \
ENUMPH(IRQ9_Time0) ENUMPH(IRQ9_Time1) ENUMPH(IRQ9_Time2) ENUMPH(IRQ9_Time3) \
ENUMPH(IRQ9_DMA0) ENUMPH(IRQ9_DMA1) ENUMPH(IRQ9_DMA2) ENUMPH(IRQ9_DMA3) \
ENUMPH(IRQ9_Keypad) \
ENUMPH(IRQ9_AGBPak) \
ENUMPH(IRQ9_IPCSync) ENUMPH(IRQ9_IPCFIFOEmpty) ENUMPH(IRQ9_IPCFIFONotEmpty) \
ENUMPH(IRQ9_NTRCardTranferComplete) ENUMPH(IRQ9_NTRCard) \
ENUMPH(IRQ9_GXFIFO) \
 \
ENUMPH(IRQ7_VBlank) ENUMPH(IRQ7_HBlank) ENUMPH(IRQ7_VCount) \
ENUMPH(IRQ7_Time0) ENUMPH(IRQ7_Time1) ENUMPH(IRQ7_Time2) ENUMPH(IRQ7_Time3) \
ENUMPH(IRQ7_SIO) \
ENUMPH(IRQ7_DMA0) ENUMPH(IRQ7_DMA1) ENUMPH(IRQ7_DMA2) ENUMPH(IRQ7_DMA3) \
ENUMPH(IRQ7_Keypad) \
ENUMPH(IRQ7_AGBPak) \
ENUMPH(IRQ7_IPCSync) ENUMPH(IRQ7_IPCFIFOEmpty) ENUMPH(IRQ7_IPCFIFONotEmpty) \
ENUMPH(IRQ7_NTRCardTranferComplete) ENUMPH(IRQ7_NTRCard) \
ENUMPH(IRQ7_Lid) \
ENUMPH(IRQ7_SPI) \
ENUMPH(IRQ7_WiFi) \
 \
ENUMPH(ARM9) ENUMPH(ARM9WBFill) ENUMPH(ARM9BIU) \
ENUMPH(DMA90) ENUMPH(DMA91) ENUMPH(DMA92) ENUMPH(DMA93) \
ENUMPH(Timer90CR) ENUMPH(Timer91CR) ENUMPH(Timer92CR) ENUMPH(Timer93CR) \
ENUMPH(Timer90Run) ENUMPH(Timer91Run) ENUMPH(Timer92Run) ENUMPH(Timer93Run) \
ENUMPH(BusReq9_DMA0) ENUMPH(BusReq9_DMA1) ENUMPH(BusReq9_DMA2) ENUMPH(BusReq9_DMA3) \
ENUMPH(BusReq9_NDMA0) ENUMPH(BusReq9_NDMA1) ENUMPH(BusReq9_NDMA2) ENUMPH(BusReq9_NDMA3) \
ENUMPH(BusReq9_ARM9) ENUMPH(Bus9CB) ENUMPH(Bus9) \
ENUMPH(Divider) ENUMPH(Sqrt) \
ENUMPH(GX) \
 \
ENUMPH(ARM7) ENUMPH(ARM7DataRead) ENUMPH(ARM7InstrRead) ENUMPH(ARM7DataWrite) \
ENUMPH(SCapDMA70) ENUMPH(SCapDMA71) \
ENUMPH(SndDMA70) ENUMPH(SndDMA71) ENUMPH(SndDMA72) ENUMPH(SndDMA73) ENUMPH(SndDMA74) ENUMPH(SndDMA75) ENUMPH(SndDMA76) ENUMPH(SndDMA77) \
ENUMPH(SndDMA78) ENUMPH(SndDMA79) ENUMPH(SndDMA7A) ENUMPH(SndDMA7B) ENUMPH(SndDMA7C) ENUMPH(SndDMA7D) ENUMPH(SndDMA7E) ENUMPH(SndDMA7F) \
ENUMPH(DMA70) ENUMPH(DMA71) ENUMPH(DMA72) ENUMPH(DMA73) \
ENUMPH(CGBSndAPUDIV) \
ENUMPH(CGBSnd1) ENUMPH(CGBSnd2) ENUMPH(CGBSnd3) ENUMPH(CGBSnd4) \
ENUMPH(TimerSnd0CR) ENUMPH(TimerSnd1CR) ENUMPH(TimerSnd2CR) ENUMPH(TimerSnd3CR) ENUMPH(TimerSnd4CR) ENUMPH(TimerSnd5CR) ENUMPH(TimerSnd6CR) ENUMPH(TimerSnd7CR) \
ENUMPH(TimerSnd8CR) ENUMPH(TimerSnd9CR) ENUMPH(TimerSndACR) ENUMPH(TimerSndBCR) ENUMPH(TimerSndCCR) ENUMPH(TimerSndDCR) ENUMPH(TimerSndECR) ENUMPH(TimerSndFCR) \
ENUMPH(TimerSnd0Run) ENUMPH(TimerSnd1Run) ENUMPH(TimerSnd2Run) ENUMPH(TimerSnd3Run) ENUMPH(TimerSnd4Run) ENUMPH(TimerSnd5Run) ENUMPH(TimerSnd6Run) ENUMPH(TimerSnd7Run) \
ENUMPH(TimerSnd8Run) ENUMPH(TimerSnd9Run) ENUMPH(TimerSndARun) ENUMPH(TimerSndBRun) ENUMPH(TimerSndCRun) ENUMPH(TimerSndDRun) ENUMPH(TimerSndERun) ENUMPH(TimerSndFRun) \
ENUMPH(Timer70CR) ENUMPH(Timer71CR) ENUMPH(Timer72CR) ENUMPH(Timer73CR) \
ENUMPH(Timer70Run) ENUMPH(Timer71Run) ENUMPH(Timer72Run) ENUMPH(Timer73Run) \
ENUMPH(PakPrefetch) \
ENUMPH(Bus7Comp) ENUMPH(Bus7Grant) \
ENUMPH(SPI) \
ENUMPH(MixAudio) \
 \
ENUMPH(IO9) ENUMPH(IO7) ENUMPH(IOAGB) \
ENUMPH(MainRAM) \
 \
ENUMPH(Scanline) ENUMPH(HBlank) \
ENUMPH(AGBScanline) ENUMPH(AGBHBlank) \
ENUMPH(CardROM) ENUMPH(CardSPI9) ENUMPH(CardSPI7) \
 \
ENUMPH(ConsolePowerOff) \
ENUMPH(EndFrame) \
 \
ENUMPH(Invalid) \
ENUMPH(Max)

#define ENUMPH(name) Evt_##name ,

typedef enum : u8 { ENUM_NAMES } Scheduler_Events;

#undef ENUMPH
#define ENUMPH(name) #name ,

static const char* Scheduler_Events_Names[] = { ENUM_NAMES };

#undef ENUMPH
#undef ENUM_NAMES

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

// convert system clock to standardized scheduler clock
timestamp DSClkSys(timestamp ts, bool agb);
 // convert 16 mhz clock to standardized scheduler clock
timestamp DSClk16(timestamp ts);
 // convert 33 mhz clock to standardized scheduler clock
timestamp DSClk33(timestamp ts);
 // convert 67 mhz clock to standardized scheduler clock
timestamp DSClk67(timestamp ts);

 // align standardized scheduler clock with system clock
timestamp DSClkAlignSys(timestamp ts, bool agb);
 // align standardized scheduler clock with 33 mhz clock
timestamp DSClkAlign33(timestamp ts);


void Sched_Log(Console* sys);
void Sched_Reset(Sched* sched);
void Sched_RemoveEvent(Sched* sched, Scheduler_Events id);
timestamp Sched_GetTime(Sched* sched, Scheduler_Events id);
bool Sched_CheckEventScheduled(Console* sys, Scheduler_Events id);
void Sched_AddEvent(Console* sys, timestamp time, Scheduler_Events id);
void Sched_AddEventIfEarlier(Console* sys, timestamp time, Scheduler_Events id);
Core_Ret Sched_RunEvent(Console* sys);

void DS_BREAK(Console* sys);
