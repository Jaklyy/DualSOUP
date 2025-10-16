#pragma once

#include "utils.h"
#include "arm/arm9/arm.h"
#include "arm/arm7/arm.h"
#include "dma/dma.h"


// system clocks
// not sure if these are actually going to be used for anything but they're useful reminders
// Source: gbatek

constexpr int Base_Clock   = 16'756'991;     // Clock without any extra multipliers applied.
constexpr int NTRBus_Clock = Base_Clock * 2; // Clock used for the main buses.
constexpr int NTR7_Clock   = Base_Clock * 2; // ARM7 Clock.
constexpr int NTR9_Clock   = Base_Clock * 4; // NARM9 Clock.

// audio clocks
// source: gbatek 
// these numbers seem odd? not sure if these should actually be
constexpr int SoundMixerFreq = 1048760; // >(1/16); (is this info relevant?)
constexpr int SoundMixerOutput = 32768; // 

// yoinked from melonDS; should be validated personally.
// they're written this way in melonDS; I'm not sure why? Probably makes sense with 2d gpu knowledge.
constexpr int Scanline_Cycles   = 355*3; // total length of a scanline in 16 MHz cycles.
constexpr int Frame_Cycles      = Scanline_Cycles * 263; // total frame length.
constexpr long double FPS       = 59.8260982881; // how do I represent this losslessly.
constexpr long double Framems   = 16.7151131131; // see above.
constexpr long double VCountus  = 63.5555631677; // length of a scanline in us; see above.



constexpr int MainRAM_Size      = MiB(4);
constexpr int SharedWRAM_Size   = KiB(32);
constexpr int ARM7WRAM_Size     = KiB(64);
constexpr int NTRBios9_Size     = KiB(4);
constexpr int NTRBios7_Size     = KiB(16);
constexpr int VRAM_A_Size       = KiB(128);
constexpr int VRAM_B_Size       = KiB(128);
constexpr int VRAM_C_Size       = KiB(128);
constexpr int VRAM_D_Size       = KiB(128);
constexpr int VRAM_E_Size       = KiB(64);
constexpr int VRAM_F_Size       = KiB(16);
constexpr int VRAM_G_Size       = KiB(16);
constexpr int VRAM_H_Size       = KiB(32);
constexpr int VRAM_I_Size       = KiB(16);



enum Scheduler_Events
{
    Sched_DMA9,
    Sched_Timer9IRQ,

    Sched_DMA7,

    Sched_MAX,
};


struct Console
{
    coroutine HandleARM9;
    coroutine HandleARM7;

    struct ARM946ES ARM9;
    struct ARM7TDMI ARM7;

    struct DMA_Channel DMA9[4];
    struct DMA_Channel DMA7[4];

    struct
    {
        u64 EventMask;
        timestamp EventTimes[Sched_MAX];
    } Scheduler;

    struct
    {
    } IO;

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
    // BIOS
    MEMORY(NTRBios9,    NTRBios9_Size);
    MEMORY(NTRBios7,    NTRBios7_Size);
};

// initialize a console to a clean state.
// if a nullptr is passed then it will allocate and initialize a console from scratch.
// otherwise it will re-initialize an already allocated struct.
// returns success or failure.
bool Console_Init(struct Console* sys);
// emulate a hardware reset.
void Console_Reset(struct Console* sys);
// actually run the emulation.
void Console_MainLoop(struct Console* sys);
