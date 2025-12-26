#include "dma.h"
#include "../console.h"
#include "../scheduler.h"




timestamp DMA_CheckNext(struct DMA_Controller* cnt, u8* id)
{
    timestamp time = timestamp_max;

    int max = stdc_trailing_zeros(cnt->CurMask) - 1;
    if (max > 3) max = 3;
    for (int i = max; i >= 0; i--)
    {
        if (time >= cnt->ChannelTimestamps[i])
        {
            time = cnt->ChannelTimestamps[i];
            *id = i;
        }
    }
    return time;
}

timestamp DMA_GetNext(struct Console* sys, bool a9)
{
    return (a9) ? sys->DMA9.NextTime : sys->DMA7.NextTime;
}

void DMA9_ScheduledRun(struct Console* sys, [[maybe_unused]] timestamp now)
{
    CR_Switch(sys->HandleARM9);
}

void DMA7_ScheduledRun(struct Console* sys, [[maybe_unused]] timestamp now)
{
    CR_Switch(sys->HandleARM7);
}

void DMA_Schedule(struct Console* sys, const bool a9)
{
    struct DMA_Controller* cnt;
    if (a9) cnt = &sys->DMA9;
    else cnt = &sys->DMA7;

    u8 id = -1;
    timestamp time = DMA_CheckNext(cnt, &id);
    cnt->NextID = id;
    cnt->NextTime = time;
    //if (a9)
    //    Schedule_Event(sys, DMA9_ScheduledRun, Evt_DMA9, time);
    //else
    //    Schedule_Event(sys, DMA7_ScheduledRun, Evt_DMA7, time);
}

void StartDMA9(struct Console* sys, timestamp start, u8 mode)
{
    for (int i = 0; i < 4; i++)
    {
        if (!sys->DMA9.Channels[i].CR.Enable) continue;
        if (sys->DMA9.Channels[i].CurrentMode != mode) continue;
        // checkme: starting dma while already started?
        sys->DMA9.ChannelTimestamps[i] = start;
    }
    DMA_Schedule(sys, true);
}

void StartDMA7(struct Console* sys, timestamp start, u8 mode)
{
    for (int i = 0; i < 4; i++)
    {
        if (!sys->DMA7.Channels[i].CR.Enable) continue;
        if (sys->DMA7.Channels[i].CurrentMode != mode) continue;
        // checkme: starting dma while already started?
        sys->DMA7.ChannelTimestamps[i] = start;
    }
    DMA_Schedule(sys, false);
}


void DMA7_Enable(struct Console* sys, struct DMA_Channel* channel, u8 channel_id)
{
    channel->Latched_SrcAddr = channel->SrcAddr;
    channel->Latched_DstAddr = channel->DstAddr;

    switch(channel->CR.StartMode7)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
        StartDMA7(sys, sys->AHB7.Timestamp+1, DMAStart_Immediate);
        break;
    }
    case 1: // VBlank
    {
        channel->CurrentMode = DMAStart_VBlank;
        break;
    }
    case 2: // NTR Gamecard
    {
        channel->CurrentMode = DMAStart_NTRCard;
        break;
    }
    #if 0
    case 3: // (DMA 0 & 2) WiFi IRQ / (DMA 1 & 3) AGB Cartridge IRQ
    {
        channel->CurrentMode = (channel_id & 0b01) ? DMAStart_AGBPakIRQ : DMAStart_WiFiIRQ;
        // agb cart dma is probably based on the cart slot irq pin?
        // wifi irq might not actually be real?
        break;
    }
    #endif
    default: LogPrint(LOG_UNIMP | LOG_DMA, "UNIMPLEMENTED DMA7 MODE %i\n", channel->CR.StartMode7); break;
    }

    switch(channel->CR.DestCR)
    {
    case 0: channel->DstInc = 1; break;
    case 1: channel->DstInc = -1; break;
    case 2: channel->DstInc = 0; break;
    case 3: channel->DstInc = 1; break;
    }

    switch(channel->CR.SourceCR)
    {
    case 0: channel->SrcInc = 1; break;
    case 1: channel->SrcInc = -1; break;
    case 2: channel->SrcInc = 0; break;
    case 3: channel->SrcInc = 1; break;
    }

    if (channel->CR.Width32)
    {
        channel->SrcInc *= 4;
        channel->DstInc *= 4;
    }
    else
    {
        channel->SrcInc *= 2;
        channel->DstInc *= 2;
    }
}

void DMA9_Enable(struct Console* sys, struct DMA_Channel* channel)
{
    channel->Latched_SrcAddr = channel->SrcAddr;
    channel->Latched_DstAddr = channel->DstAddr;

    switch(channel->CR.StartMode9)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
        StartDMA9(sys, sys->AHB9.Timestamp+1, DMAStart_Immediate);
        break;
    }
    case 1: // VBlank
    {
        channel->CurrentMode = DMAStart_VBlank;
        break;
    }
    case 2: // HBlank (excl. VBlank)
    {
        channel->CurrentMode = DMAStart_HBlank;
        break;
    }
    case 5: // NTR Gamecard
    {
        channel->CurrentMode = DMAStart_NTRCard;
        break;
    }
    case 7: // 3D Command FIFO
    {
        channel->CurrentMode = DMAStart_3DFIFO;

        if (sys->GX3D.Status.FIFOHalfEmpty)
            StartDMA9(sys, sys->AHB9.Timestamp+1, DMAStart_3DFIFO);
        break;
    }
    #if 0
    case 3: // synchronize to start of display(??)
    {
        channel->CurrentMode = DMAStart_Video;
        // not sure what this means actually...?
        // melonds seems to run it once per vcount for vcounts 2 - 193; and then explicitly tries to stop them on vcount 194?
        break;
    }
    case 4: // Display FIFO
    {
        channel->CurrentMode = DMAStart_DisplayFIFO;
        break;
    }
    case 6: // AGB Gamepak
    {
        channel->CurrentMode = DMAStart_AGBPakIRQ;
        // TODO: how does this work?
        // is it even hooked up?
        break;
    }
    #endif
    default: LogPrint(LOG_UNIMP | LOG_DMA, "UNIMPLEMENTED DMA9 MODE %i\n", channel->CR.StartMode9); break;
    }

    switch(channel->CR.DestCR)
    {
    case 0: channel->DstInc = 1; break;
    case 1: channel->DstInc = -1; break;
    case 2: channel->DstInc = 0; break;
    case 3: channel->DstInc = 1; break;
    }

    switch(channel->CR.SourceCR)
    {
    case 0: channel->SrcInc = 1; break;
    case 1: channel->SrcInc = -1; break;
    case 2: channel->SrcInc = 0; break;
    case 3: channel->SrcInc = 1; break;
    }

    if (channel->CR.Width32)
    {
        channel->SrcInc *= 4;
        channel->DstInc *= 4;
    }
    else
    {
        channel->SrcInc *= 2;
        channel->DstInc *= 2;
    }
}

void DMA_Run(struct Console* sys, const bool a9)
{
    struct DMA_Controller* cnt = ((a9) ? &sys->DMA9 : &sys->DMA7);
    u8 id = cnt->NextID;
    if (cnt->CurMask & (1<<id)) return;
    u32 rmask;
    u32 wmask;
    struct DMA_Channel* channel = &cnt->Channels[id];
    u64 timecur = cnt->ChannelTimestamps[id];
    cnt->ChannelTimestamps[id] = timestamp_max;

    // CHECKME: idk, where and when things are latched needs testing.
    if (channel->CR.SourceCR == 3) channel->Latched_SrcAddr = channel->SrcAddr;
    if (channel->CR.DestCR == 3) channel->Latched_DstAddr = channel->DstAddr;

    if (channel->Latched_NumWords == 0) channel->Latched_NumWords = (channel->CR.NumWords ? channel->CR.NumWords : 0x200000);


    cnt->CurMask |= 1<<id;
    DMA_Schedule(sys, a9);

    if (channel->CR.Width32)
    {
        rmask = (wmask = u32_max);
    }

    bool rseq = false;
    bool wseq = false;
    bool tseq = false;
    u32 numword = channel->Latched_NumWords;
    if (channel->CurrentMode == DMAStart_3DFIFO)
    {
        if (numword > 112) numword = 112;
    }

    while(numword > 0)
    {
        if (!channel->CR.Width32)
        {
            rmask = ROR32(u16_max, (channel->Latched_SrcAddr & 2)*8);
            wmask = ROR32(u16_max, (channel->Latched_DstAddr & 2)*8);
        }

        if (!AHB_NegOwnership(sys, &timecur, false, a9))
        {
            rseq = false;
            wseq = false;
            tseq = false; // checkme
        }
        timestamp diff = timecur;
        u32 read;
        if (a9)
        {
            read = AHB9_Read(sys, &timecur, channel->Latched_SrcAddr, rmask, false, true, &rseq, true);
        }
        else
        {
            read = AHB7_Read(sys, &timecur, channel->Latched_SrcAddr, rmask, false, true, &rseq, true, 0xFFFFFFFF /*checkme?*/);
        }
        diff = timecur - diff;

        channel->Latched_SrcAddr += channel->SrcInc;
        if (!AHB_NegOwnership(sys, &timecur, false, a9))
        {
            rseq = false;
            wseq = false;
            tseq = false; // checkme
        }
        if (a9)
        {
            AHB9_Write(sys, &timecur, channel->Latched_DstAddr, read, wmask, false, &wseq, true);
        }
        else
        {
            AHB7_Write(sys, &timecur, channel->Latched_DstAddr, read, wmask, false, &wseq, true, 0xFFFFFFFF /*checkme?*/);
        }
        // CHECKME: should this only apply to the actual first?
        if (!tseq)
        {
            // TODO: figure out why exactly this happens?
            if (diff == 1)
                timecur +=1;
        }
        tseq = true;

        rseq = (channel->SrcInc > 0);
        wseq = (channel->DstInc > 0);

        channel->Latched_DstAddr += channel->DstInc;

        channel->Latched_NumWords -= 1;
        numword -= 1;
    }

    //Bus_MainRAM_ReleaseHold(sys, a9 ? &sys->AHB9 : &sys->AHB7); why isn't this necessary?

    // end

    if (channel->Latched_NumWords == 0)
    {
        if (channel->CR.Repeat)
        {
            // TODO: reschedule
            LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: DMA WANTS RESCHEDULE %i\n", channel->CurrentMode);
        }
        else
        {
            channel->CR.Enable = false;
        }
    }

    cnt->CurMask &= ~1<<id;

    //cnt->ChannelLastEnded[id] = timecur;

    if (cnt->ChannelTimestamps[id] < timecur)
        cnt->ChannelTimestamps[id] = timecur;

    if (channel->CR.IRQ) // TODO
        Console_ScheduleIRQs(sys, IRQ_DMA0+id, a9, timecur); // checkme: delay

    DMA_Schedule(sys, a9);
}

u32 DMA_IOReadHandler(struct DMA_Channel* channels, u32 addr)
{
    addr &= 0xFF;
    addr -= 0xB0;
    int channel = (addr / 4) / 3;
    int reg = (addr / 4) % 3;

    struct DMA_Channel* cur = &channels[channel];

    switch(reg)
    {
    case 0: // source address
    {
        return cur->SrcAddr;
    }
    case 1: // destination address
    {
        return cur->DstAddr;
    }
    case 2: // control register
    {
        return cur->CR.Raw;
    }
    default:
        return 0;
    }
}

void DMA9_IOWriteHandler(struct Console* sys, struct DMA_Channel* channels, u32 addr, u32 val, u32 mask)
{
    addr &= 0xFF;
    addr -= 0xB0;
    int channel = (addr / 4) / 3;
    int reg = (addr / 4) % 3;

    struct DMA_Channel* cur = &channels[channel];

    switch(reg)
    {
    case 0: // source address
    {
        mask &= 0x0FFFFFFE;

        MaskedWrite(cur->SrcAddr, val, mask);
        break;
    }
    case 1: // destination address
    {
        mask &= 0x0FFFFFFE;

        MaskedWrite(cur->DstAddr, val, mask);
        break;
    }
    case 2: // control register
    {
        union DMA_CR oldcr = cur->CR;
        MaskedWrite(cur->CR.Raw, val, mask);

        if (oldcr.Enable ^ cur->CR.Enable)
        {
            if (cur->CR.Enable == true)
            {
                // starting dma channel
                DMA9_Enable(sys, cur);
            }
            else
            {
                LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: Stopping DMA9\n");
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
}

void DMA7_IOWriteHandler(struct Console* sys, struct DMA_Channel* channels, u32 addr, u32 val, const u32 mask)
{
    addr &= 0xFF;
    addr -= 0xB0;
    int channel = (addr / 4) / 3;
    int reg = (addr / 4) % 3;

    struct DMA_Channel* cur = &channels[channel];

    switch(reg)
    {
    case 0: // source address
    {
        val &= ((channel == 0) ? 0x07FFFFFE : 0x0FFFFFFE);

        MaskedWrite(cur->SrcAddr, val, mask);
        break;
    }
    case 1: // destination address
    {
        val &= ((channel == 3) ? 0x0FFFFFFE : 0x07FFFFFE);

        MaskedWrite(cur->DstAddr, val, mask);
        break;
    }
    case 2: // control register
    {
        val &= ((channel == 3) ? 0xF7E0FFFF : 0xF7E03FFF);

        union DMA_CR oldcr = cur->CR;
        MaskedWrite(cur->CR.Raw, val, mask);

        if (oldcr.Enable ^ cur->CR.Enable)
        {
            if (cur->CR.Enable == true)
            {
                // starting dma channel
                DMA7_Enable(sys, cur, channel);
            }
            else
            {
                LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: Stopping DMA7\n");
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
}
