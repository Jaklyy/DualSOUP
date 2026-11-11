#pragma once
#include "core/utils.h"

typedef struct
{
    u8 Pace;
    u8 Cur;
    u16 Freq;
    bool On;
} CGBSquareSweep;

typedef struct
{
    bool Dir;
    u8 Cur;
    s8 Vol;
} CGBEnvelope;

typedef struct
{
    CGBSquareSweep Swp;
    CGBEnvelope Env;
    u8 Len;
    u16 Counter;
    u8 Tick;
} CGBSquareLatch;

typedef union
{
    u32 Raw;
    struct {
        u32 SweepShift : 3;
        bool SweepDir : 1;
        u32 SweepPace : 3;
        u32 : 9;
        u32 Len : 6;
        u32 WaveDuty : 2;
        u32 EnvelopePace : 3;
        bool EnvelopeDir : 1;
        u32 EnvelopeVol : 4;
    };
} CGBSndSquare;

typedef union
{
    u32 Raw;
    struct {
        u32 : 5;
        bool WaveDimension : 1;
        bool WaveBank : 1;
        bool DACEnable : 1;
        u32 : 8;
        u32 Len : 8;
        u32 : 5;
        u32 Vol : 3;
    };
} CGBSndWave;

typedef union
{
    u16 Raw;
    struct {
        u16 Freq : 11;
        u16 : 3;
        bool UseLen : 1;
        bool Start : 1;
    };
} CGBSndCR;

typedef union
{
    u16 Raw;
    struct {
        u16 Len : 6;
        u16 : 2;
        u16 EnvelopePace : 3;
        bool EnvelopeDir : 1;
        u16 InitialVol : 4;
    };
} CGBSndNoise;

typedef union
{
    u16 Raw;
    struct {
        u16 ClkDivider : 3;
        bool ShortLSFR : 1;
        u16 ClkShift : 4;
        u16 : 6;
        bool UseLen : 1;
        bool Start : 1;
    };
} CGBSndNoiseCR;

typedef union
{
    u32 Raw;
    struct {
        u32 RightVolume : 3;
        u32 : 1;
        u32 LeftVolume : 3;
        u32 : 1;
        bool Chan1REn : 1;
        bool Chan2REn : 1;
        bool Chan3REn : 1;
        bool Chan4REn : 1;
        bool Chan1LEn : 1;
        bool Chan2LEn : 1;
        bool Chan3LEn : 1;
        bool Chan4LEn : 1;
        u32 CGBVol : 2;
        bool AVol : 1;
        bool BVol : 1;
        u32 : 4;
        bool AREn : 1;
        bool ALEn : 1;
        bool ATimerSel : 1;
        bool AFIFOReset : 1;
        bool BREn : 1;;
        bool BLEn : 1;
        bool BTimerSel : 1;
        bool BFIFOReset : 1;
    };
} AGBSndCR;

typedef union
{
    u8 Raw;
    struct {
    bool Snd1Running : 1;
    bool Snd2Running : 1;
    bool Snd3Running : 1;
    bool Snd4Running : 1;
    u8 : 3;
    bool MEnable : 1;
    };
} AGBSndEn;

// 32768 cycles @ 16MHz == 512 hz
#define GB_DIVAPUTICK(x) (DSClk16(32768)*x)

typedef struct Console Console;


void AGBMixer_Run(Console* sys);

void AGBSquare_Run(Console* sys, const timestamp now, const u8 id);
void AGBWave_Run(Console* sys, const timestamp now);
void AGBNoise_Run(Console* sys, const timestamp now);

void AGBSnd_APUDivTick(Console* sys, const timestamp now);

void AGBSoundDMATimer_Overflow(Console* sys, const u8 id, const timestamp now);

void AGBSound_IORead(Console* sys, const u32 addr, u32* rdata);
void AGBSound_IOWrite(Console* sys, const u32 addr, const u32 wrdata, const u32 mask, const timestamp now);
