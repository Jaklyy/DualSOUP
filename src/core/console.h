#pragma once

#include "utils.h"
#include "arm/arm9/arm.h"
#include "arm/arm7/arm.h"


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



constexpr int MainRAM_Size      = 4   * 1024 * 1024; // 4   MiB
constexpr int SharedWRAM_Size   = 32  * 1024;        // 32  KiB
constexpr int ARM7WRAM_Size     = 64  * 1024;        // 64  KiB
constexpr int NTRBios9_Size     = 4   * 1024;        // 4   KiB
constexpr int NTRBios7_Size     = 16  * 1024;        // 16  KiB
constexpr int VRAM_A_Size       = 128 * 1024;        // 128 KiB
constexpr int VRAM_B_Size       = 128 * 1024;        // 128 KiB
constexpr int VRAM_C_Size       = 128 * 1024;        // 128 KiB
constexpr int VRAM_D_Size       = 128 * 1024;        // 128 KiB
constexpr int VRAM_E_Size       = 64  * 1024;        // 64  KiB
constexpr int VRAM_F_Size       = 16  * 1024;        // 16  KiB
constexpr int VRAM_G_Size       = 16  * 1024;        // 16  KiB
constexpr int VRAM_H_Size       = 32  * 1024;        // 32  KiB
constexpr int VRAM_I_Size       = 16  * 1024;        // 16  KiB



struct NDS
{
    struct ARM946ES ARM9;
    struct ARM7TDMI ARM7;

    struct
    {
        
    } IO;

    alignas(64)
    u8 MainRAM[MainRAM_Size]; // fcram
    u8 SharedWRAM[SharedWRAM_Size];
    u8 ARM7WRAM[ARM7WRAM_Size];
    u8 VRAM_A[VRAM_A_Size];
    u8 VRAM_B[VRAM_B_Size];
    u8 VRAM_C[VRAM_C_Size];
    u8 VRAM_D[VRAM_D_Size];
    u8 VRAM_E[VRAM_E_Size];
    u8 VRAM_F[VRAM_F_Size];
    u8 VRAM_G[VRAM_G_Size];
    u8 VRAM_H[VRAM_H_Size];
    u8 VRAM_I[VRAM_I_Size];
    u8 NTRBios9[NTRBios9_Size];
    u8 NTRBios7[NTRBios7_Size];
};