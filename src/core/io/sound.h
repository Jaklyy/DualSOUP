#pragma once
#include "../utils.h"



enum AudioFormat
{
    AudioFormat_PCM8,
    AudioFormat_PCM16,
    AudioFormat_ADPCM,
    AudioFormat_PSGNoise,
};

constexpr s8 ADPCM_IndexTable[] = 
{
    -1, -1, -1, -1,
     2,  4,  6,  8
};

constexpr u16 ADPCM_Table[] =
{
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
    0x0010, 0x0011, 0x0013, 0x0015, 0x0017, 0x0019, 0x001C, 0x001F,
    0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042,
    0x0049, 0x0050, 0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F,
    0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117, 0x0133,
    0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292,
    0x02D4, 0x031C, 0x036C, 0x03C3, 0x0424, 0x048E, 0x0502, 0x0583,
    0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0,
    0x0CFF, 0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954,
    0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF, 0x315B, 0x364B,
    0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462,
    0x7FFF,
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
    u8 FIFO_FillPtr;
    u8 FIFO_DrainPtr;
    u8 FIFO_Bytes;
    u32 Prog;
    u64 SampleMax;
    u32 DMAINIT;
    u32 DMAINIT2;

    s16 CurSample;

    u16 Noise_Cur;

    u8 ADPCM_Data;
    s32 ADPCM_Sample;
    s16 ADPCM_LoopSample;
    s8 ADPCM_Index;
    s8 ADPCM_LoopIndex;
    u64 ADPCM_LoopStart;
    u64 ADPCM_LoopEnd;
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
void AudioMixer_Run(struct Console* sys, timestamp now);

void SoundChannel_TryStartAll(struct Console* sys, const timestamp now);
void SoundChannel_KillAll(struct Console* sys, const timestamp now);
