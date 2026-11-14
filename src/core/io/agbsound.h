#pragma once
#include "core/utils.h"




typedef union {
    u8 Raw;
    struct {
        u8 Shift : 3;
        bool Dec : 1;
        u8 Pace : 3;
    };
} CGBSndSweep;

typedef union {
    u8 Raw;
    struct {
        u8 Pace : 3;
        bool Inc : 1;
        u8 StartVol : 4;
    };
    struct {
        u8 : 3;
        u8 DACEnable : 5; // if any bit is 1 the DAC is enabled
    };
} CGBSndEnvelope;

typedef struct {
    u8 Tick;
    u8 Vol;
} CGBSndEnvelopeLatch;

typedef struct
{
    struct {
        union {
            u32 RawLoC1;
            struct {
                u16 : 16;
                u16 RawLoC2;
            };
            struct {
                CGBSndSweep Sweep;
                u8 : 8; // unused
                u8 Length : 6; // write only
                u8 DutyPattern : 2;
                CGBSndEnvelope Envelope;
            };
        };
        union {
            u16 RawHi;
            struct {
                u16 Freq : 11; // write only
                u16 : 3;
                bool UseLength : 1;
                bool Trigger : 1; // write only
            };
        };
    } Regs;
    struct {
        struct {
            u8 Tick;
            u16 Freq;
            bool On;
        } Sweep;
        CGBSndEnvelopeLatch Envelope;
        u8 LenTick;
        u8 DutyTick;
    } Latched;
} CGBSndPulse;

typedef struct
{
    struct {
        union {
            u32 RawLo;
            struct {
                u32 : 5;
                bool UseTwoBanks : 1;
                bool BankSelect : 1;
                bool DACEnable : 1;
                u32 : 8;
                u32 Length : 8; // write only
                u32 : 5;
                u32 Vol : 3;
            };
        };
        union {
            u16 RawHi;
            struct {
                u16 Freq : 11; // write only
                u16 : 3;
                bool UseLength : 1;
                bool Trigger : 1; // write only
            };
        };
    } Regs;
    struct {
        bool BankCur;
        u8 BankProg;
        u16 LenTick;
    } Latched;
} CGBSndWave;

typedef struct
{
    struct {
        union {
            u16 RawLo;
            struct {
                u8 Length : 6; // write only
                u8 : 2;
                CGBSndEnvelope Envelope;
            };
        };
        union {
            u16 RawHi;
            struct {
                u16 ClkDivider : 3;
                bool ShortLSFR : 1;
                u16 ClkShift : 4;
                u16 : 6;
                bool UseLength : 1;
                bool Trigger : 1; // write only
            };
        };
    } Regs;
    struct
    {
        CGBSndEnvelopeLatch Envelope;
        u16 LFSR;
        u8 LenTick;
    } Latched;
} CGBSndNoise;

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
    bool MasterEnable : 1;
    };
} AGBSndEn;

// 32768 cycles @ 16MHz == 512 hz
#define GB_DIVAPUTICK(x) (DSClk16(32768)*x)

typedef struct Console Console;


void AGBMixer_Run(Console* sys);

void CGBSnd_KillChannel(Console* sys, const u8 id);

void AGBPulse_Run(Console* sys, const timestamp now, const u8 id);
void AGBWave_Run(Console* sys, const timestamp now);
void AGBNoise_Run(Console* sys, const timestamp now);

void AGBSnd_APUDivTick(Console* sys, const timestamp now);

void AGBSoundDMATimer_Overflow(Console* sys, const u8 id, const timestamp now);

void AGBSound_IORead(Console* sys, const u32 addr, u32* rdata);
void AGBSound_IOWrite(Console* sys, const u32 addr, const u32 wrdata, const u32 mask, const timestamp now);
