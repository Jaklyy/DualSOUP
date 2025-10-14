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
    DMAStart_AGBCartIRQ,
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
        bool DMARepeat : 1;
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
    u8 CurrentMode;
};

struct DMA_Controller
{
    struct DMA_Channel Channels[4];
    timestamp ChannelTimestamps[4];
};

void DMA7_IOWriteHandler(struct DMA_Channel channels[4], u32 addr, u32 val, const u32 mask);
