#pragma once
#include "../utils.h"



enum AudioFormat
{
    AudioFormat_PCM8,
    AudioFormat_PCM16,
    AudioFormat_ADPCM,
    AudioFormat_PSGNoise,
};

typedef struct
{
    union
    {
        u32 Raw;
        struct
        {
            u32 VolumeMultiplier : 7;
            u32 : 1;
            u32 VolumeDivider : 2;
            u32 : 5;
            bool Hold : 1;
            u32 Panning : 7;
            u32 : 1;
            u32 WaveDuty : 3;
            u32 RepeatMode : 2;
            u32 Format : 2;
            bool Enable : 1;
        };
    } CR;
    u32 SrcAddr;
    u16 Timer;
    u16 LoopOffs;
    u32 SoundLen;
    MEMORY(FIFO, 32);
    u8 FIFOFillPtr;
    u8 FIFODrainPtr;
    bool FIFOSatiated;
    u32 Prog;
    u64 SampleMax;
} SoundChannel;

typedef struct
{
    union
    {
        u8 Raw;
        struct
        {
            bool Addition : 1;
            bool Source : 1;
            bool Repeat : 1;
            bool Format : 1;
            u8 : 3;
            bool Enable : 1;
        };
    } CR;
        u32 DstAddr;
        u16 Length;
} SoundCapture;

struct Console;
u32 SoundChannel_IORead(struct Console* sys, const u32 addr, const timestamp now);
void SoundChannel_IOWrite(struct Console* sys, const u32 addr, const u32 val, const u32 mask, const timestamp now);

void SoundFIFO_Fill(struct Console* sys, const u32 val, const u8 id, const timestamp now);
void SoundFIFO_Sample(struct Console* sys, const u8 id, const timestamp now);

void SoundChannel_TryStartAll(struct Console* sys, const timestamp now);
void SoundChannel_KillAll(struct Console* sys, const timestamp now);
