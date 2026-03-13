#pragma once
#include "../utils.h"




enum DMA_StartModes : u8
{
    DMAStart_Null,
    DMAStart_Immediate,
    DMAStart_VBlank,
    DMAStart_HBlank,
    DMAStart_Video,
    DMAStart_DisplayFIFO,
    DMAStart_NTRCard,
    DMAStart_AGBPakIRQ,
    DMAStart_3DFIFO,
    DMAStart_WiFiIRQ,
    DMAStart_Audio,
    DMAStart_AudioCap,
};

union DMA_CR
{
    u32 Raw;
    struct
    {
        u32 NumWords : 21;
        u32 DestCR : 2;
        u32 SourceCR: 2;
        bool Repeat : 1;
        bool Width32 : 1;
        u32 StartMode9 : 3;
        bool IRQ : 1;
        bool Enable : 1;
    };
    struct
    {
        u32 : 27;
        bool AGBCartIRQ : 1; // agb mode only (how does this actually work...?)
        u32 StartMode7 : 2;
    };
};

constexpr int DMA7_NumSound = 16;
constexpr int DMA7_NumSoundCap = 2;
constexpr int DMA7_NumNormal = 4;
constexpr int DMA7_NumNew = 4;

constexpr int DMA7_Base = 0;

constexpr int DMA7_SoundCapBase = DMA7_Base;
constexpr int DMA7_SoundCapMax = DMA7_SoundCapBase + DMA7_NumSoundCap;

constexpr int DMA7_SoundBase = DMA7_SoundCapMax;
constexpr int DMA7_SoundMax = DMA7_SoundBase + DMA7_NumSound;

constexpr int DMA7_NormalBase = DMA7_SoundMax;
constexpr int DMA7_NormalMax = DMA7_NormalBase + DMA7_NumNormal;

constexpr int DMA7_NewBase = DMA7_NormalMax;
constexpr int DMA7_NewMax = DMA7_NewBase + DMA7_NumNew;

constexpr int DMA7_Max = DMA7_NewMax;


struct DMA_Channel
{
    u32 SrcAddr;
    u32 DstAddr;
    union DMA_CR CR;

    u32 SrcAddrMask;
    u32 DstAddrMask;
    u32 Latched_SrcAddr;
    u32 Latched_DstAddr;
    u32 NumWords;
    u32 Latched_NumWords;
    s8 SrcInc;
    s8 DstInc;
    u8 CurrentMode;
};

struct DMA_Controller
{
    alignas(sizeof(timestamp[32])) timestamp ChannelTimestamps[DMA7_Max+1];
    timestamp NextTime;
    struct DMA_Channel Channels[DMA7_Max];
    u32 CurMask;
    u8 NextID;
};

struct Console;

void DMA_Schedule(struct Console* sys, const bool a9);
void DMA_Run(struct Console* sys, const bool a9);
void DMA7_IOWriteHandler(struct Console* sys, struct DMA_Channel* channels, u32 addr, u32 val, const u32 mask);
void DMA9_IOWriteHandler(struct Console* sys, struct DMA_Channel* channels, u32 addr, u32 val, u32 mask);
u32 DMA_IOReadHandler(struct DMA_Channel* channels, u32 addr);
void StartDMA9(struct Console* sys, timestamp start, u8 mode);
void StartDMA7(struct Console* sys, timestamp start, u8 mode);
void StartSoundCapDMA(struct Console* sys, u8 id, timestamp start);
void StartSoundDMA(struct Console* sys, u8 id, timestamp start, bool matters);
timestamp DMA_GetNext(struct Console* sys, bool a9, const bool inclusive);
