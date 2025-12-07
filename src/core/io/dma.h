#pragma once
#include "../utils.h"




enum DMA_StartModes : u8
{
    DMAStart_Immediate,
    DMAStart_VBlank,
    DMAStart_HBlank,
    DMAStart_Video,
    DMAStart_DisplayFIFO,
    DMAStart_NTRCard,
    DMAStart_AGBPakIRQ,
    DMAStart_3DFIFO,
    DMAStart_WiFiIRQ,
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

struct DMA_Channel
{
    u32 SrcAddr;
    u32 DstAddr;
    union DMA_CR CR;

    u32 Latched_SrcAddr;
    u32 Latched_DstAddr;
    u32 Latched_NumWords;
    s8 SrcInc;
    s8 DstInc;
    u8 CurrentMode;
    bool DMAQueued;
};

struct DMA_Controller
{
    alignas(sizeof(timestamp[4])) timestamp ChannelTimestamps[4];
    alignas(sizeof(timestamp[4])) timestamp ChannelLastEnded[4];
    timestamp NextTime;
    struct DMA_Channel Channels[4];
    u8 CurMask;
    s8 NextID;
};

struct Console;

void DMA_Run(struct Console* sys, const bool a9);
void DMA7_IOWriteHandler(struct Console* sys, struct DMA_Channel* channels, u32 addr, u32 val, const u32 mask);
void DMA9_IOWriteHandler(struct Console* sys, struct DMA_Channel* channels, u32 addr, u32 val, u32 mask);
u32 DMA_IOReadHandler(struct DMA_Channel* channels, u32 addr);
void StartDMA9(struct Console* sys, timestamp start, u8 mode);
void StartDMA7(struct Console* sys, timestamp start, u8 mode);
timestamp DMA_GetNext(struct Console* sys, bool a9);
