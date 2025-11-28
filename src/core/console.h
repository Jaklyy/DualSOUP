#pragma once

#include <stdio.h>
#include "bus/io.h"
#include "utils.h"
#include "arm/arm9/arm.h"
#include "arm/arm7/arm.h"
#include "dma/dma.h"
#include "bus/ahb.h"
#include "timer/timer.h"
#include "scheduler.h"
#include "irq.h"


// system clocks
// not sure if these are actually going to be used for anything but they're useful reminders
// Source: gbatek

constexpr unsigned Base_Clock   = 16'756'991;     // Clock without any extra multipliers applied.
constexpr unsigned NTRBus_Clock = Base_Clock * 2; // Clock used for the main buses.
constexpr unsigned NTR7_Clock   = Base_Clock * 2; // ARM7 Clock.
constexpr unsigned NTR9_Clock   = Base_Clock * 4; // NARM9 Clock.

// audio clocks
// source: gbatek 
// these numbers seem odd? not sure if these should actually be
constexpr unsigned SoundMixerFreq = 1048760; // >(1/16); (is this info relevant?)
constexpr unsigned SoundMixerOutput = 32768; // 

// yoinked from melonDS; should be validated personally.
// they're written this way in melonDS; I'm not sure why? Probably makes sense with 2d gpu knowledge.
constexpr unsigned ActiveRender_Cycles = 24+(256*3); // How long the PPUs are actively rendering graphics in a given scanline. (note: 24 cycles are not actually spent rendering)
constexpr unsigned HBlank_Cycles       = 91*3; // length of the HBlank period.
constexpr unsigned Scanline_Cycles     = HBlank_Cycles + ActiveRender_Cycles; // total length of a scanline in 16 MHz cycles.
constexpr unsigned Frame_Cycles        = Scanline_Cycles * 263; // total frame length.
constexpr long double FPS       = 59.8260982881; // how do I represent this losslessly.
constexpr long double Framems   = 16.7151131131; // see above.
constexpr long double VCountus  = 63.5555631677; // length of a scanline in us; see above.



constexpr unsigned MainRAM_Size     = MiB(4);
constexpr unsigned SharedWRAM_Size  = KiB(32);
constexpr unsigned ARM7WRAM_Size    = KiB(64);
constexpr unsigned NTRBios9_Size    = KiB(4);
constexpr unsigned NTRBios7_Size    = KiB(16);
constexpr unsigned VRAM_A_Size      = KiB(128);
constexpr unsigned VRAM_B_Size      = KiB(128);
constexpr unsigned VRAM_C_Size      = KiB(128);
constexpr unsigned VRAM_D_Size      = KiB(128);
constexpr unsigned VRAM_E_Size      = KiB(64);
constexpr unsigned VRAM_F_Size      = KiB(16);
constexpr unsigned VRAM_G_Size      = KiB(16);
constexpr unsigned VRAM_H_Size      = KiB(32);
constexpr unsigned VRAM_I_Size      = KiB(16);
constexpr unsigned Palette_Size     = KiB(2);
constexpr unsigned OAM_Size         = KiB(2);




union VRAMCR
{
    u8 Raw;
    struct
    {
        u32 Mode : 3;
        u32 Offset : 2;
        u32 : 2;
        bool Enable : 1;
    };
};

struct Console
{
    struct ARM946ES ARM9;
    struct ARM7TDMI ARM7;

    struct DMA_Controller DMA9;
    struct DMA_Controller DMA7;

    struct AHB AHB9;
    struct AHB AHB7;
    struct BusMainRAM BusMR;

    coroutine HandleMain;
    coroutine HandleARM9;
    coroutine HandleARM7;

    timestamp ARM9Target;
    timestamp ARM7Target;

    struct Scheduler Sched;

    alignas(HOST_CACHEALIGN) timestamp IRQSched9[IRQ_Max];
    alignas(HOST_CACHEALIGN) timestamp IRQSched7[IRQ_Max];

    u16 VCount;

    u8 IPCSyncDataTo9; // data sent to arm9
    u8 IPCSyncDataTo7; // data sent to arm7
    bool IPCSyncIRQEnableTo9; // enable for arm9
    bool IPCSyncIRQEnableTo7; // enable irqs for arm7

    bool IME9;
    bool IME7;

    alignas(u32) union VRAMCR VRAMCR[9];
    u8 WRAMCR;
    u32 IE9;
    u32 IF9;
    u32 IE7;
    u32 IF7;
    u32 DMAFill[4];
    union
    {
        u16 Raw;
        struct
        {
            u16 : 3;
            bool VBlankIRQ : 1;
            bool HBlankIRQ : 1;
            bool VCountMatchIRQ : 1;
            u16 : 1;
            u16 VCountMSB : 1;
            u16 VCountLSB : 8;
        };
    } DispStatRW;
    u16 TargetVCount;
    u16 VCountNew;
    bool VCountUpdate;
    union
    {
        u8 Raw;
        struct
        {
            bool VBlank : 1;
            bool HBlank : 1;
            bool VCountMatch : 1;
            u8 : 3;
            bool LCDReady : 1;
        };
    } DispStatRO;

    struct Timer Timers9[4];
    struct Timer Timers7[4];

    union
    {
        u8 Raw;
        struct
        {
            u8 GBARAMTimings : 2;
            u8 GBAROMTimingsNS : 2;
            u8 GBAROMTimingsSeq : 1;
            u8 GBAPHIClock : 2;
        };
    } ExtMemCR_7;
    union
    {
        u8 Raw;
        struct
        {
            u8 GBARAMTimings : 2;
            u8 GBAROMTimingsNS : 2;
            u8 GBAROMTimingsSeq : 1;
            u8 GBAPHIClock : 2;
        };
    } ExtMemCR_9;
    union
    {
        u16 Raw;
        struct
        {
            u16 : 7;
            bool GBAPakAccess : 1; // enabled = arm7
            u16 : 1;
            bool NDSCard2Access: 1; // DSI ONLY
            u16 : 1;
            bool NDSCardAccess : 1; // enabled = arm7
            u16 : 1;
            bool MRSomething1 : 1; // idk
            bool MRSomething2 : 1; // idk either
            bool MRPriority : 1; // enabled = arm7 priority
        };
    } ExtMemCR_Shared;

    alignas(HOST_CACHEALIGN)
    // FCRAM
    MEMORY(MainRAM,     MainRAM_Size);
    // WRAM
    MEMORY(SharedWRAM,  SharedWRAM_Size);
    MEMORY(ARM7WRAM,    ARM7WRAM_Size);
    // VRAM
    MEMORY(VRAM_A,      VRAM_A_Size);
    MEMORY(VRAM_B,      VRAM_B_Size);
    MEMORY(VRAM_C,      VRAM_C_Size);
    MEMORY(VRAM_D,      VRAM_D_Size);
    MEMORY(VRAM_E,      VRAM_E_Size);
    MEMORY(VRAM_F,      VRAM_F_Size);
    MEMORY(VRAM_G,      VRAM_G_Size);
    MEMORY(VRAM_H,      VRAM_H_Size);
    MEMORY(VRAM_I,      VRAM_I_Size);
    // Video Misc
    MEMORY(Palette,     Palette_Size);
    MEMORY(OAM,         OAM_Size);
    // BIOS
    MEMORY(NTRBios9,    NTRBios9_Size);
    MEMORY(NTRBios7,    NTRBios7_Size);
};

// initialize a console to a clean state.
// if a nullptr is passed then it will allocate and initialize a console from scratch.
// otherwise it will re-initialize an already allocated struct.
// returns success or failure.
struct Console* Console_Init(struct Console* sys, FILE* ntr9, FILE* ntr7);
// emulate a hardware reset.
void Console_Reset(struct Console* sys);
// actually run the emulation.
void Console_MainLoop(struct Console* sys);

void Console_DirectBoot(struct Console* sys, FILE* rom);


void Console_ScheduleIRQs(struct Console* sys, const u8 irq, const bool a9, timestamp time);
timestamp Console_GetARM7Cur(struct Console* sys);
timestamp Console_GetARM9Cur(struct Console* sys);
