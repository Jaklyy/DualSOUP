#include "dma.h"
#include "../console.h"
#include "../scheduler.h"




void DMA_ScheduleStart()
{
    // we need:
    // channel mode.
    // channel timestamp.
    // update the main scheduler
    // when the write occured.
#if 0
    switch(channel->CurrentMode)
    {
        case DMAStart_Immediate:
        {
            controller->ChannelTimestamps[curchannel] = /* bus timestamp...? */ + 1;
        }
    }
#endif
}

void DMA_Schedule(struct Console* sys, struct DMA_Controller* cnt)
{
    timestamp time = timestamp_max;
    int id = 4;

    for (unsigned i = 0; i < 4; i++)
    {
        if (time >= cnt->ChannelTimestamps[i])
        {
            time = cnt->ChannelTimestamps[i];
            id = i;
        }
    }
    cnt->NextDMAID = id;
    Schedule_Event(sys, cnt->EvtID, time);
}

timestamp DMA_CheckNext(struct Console* sys, struct DMA_Controller* cnt, u8* id)
{
    timestamp time = timestamp_max;

    int max = *id-1;
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

void DMA7_Enable(struct Console* sys, struct DMA_Channel* channel, u8 channel_id)
{
    channel->Latched_SrcAddr = channel->SrcAddr;
    channel->Latched_DstAddr = channel->DstAddr;
    channel->Latched_NumWords = (channel->CR.NumWords ? channel->CR.NumWords : ((channel_id == 3) ? 0x10000 : 0x4000));

    switch(channel->CR.StartMode7)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
        sys->DMA7.ChannelTimestamps[channel_id] = sys->AHB7.Timestamp+1;
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
    case 3: // (DMA 0 & 2) WiFi IRQ / (DMA 1 & 3) AGB Cartridge IRQ
    {
        channel->CurrentMode = (channel_id & 0b01) ? DMAStart_AGBPakIRQ : DMAStart_WiFiIRQ;
        // agb cart dma is probably based on the cart slot irq pin?
        // wifi irq might not actually be real?
        break;
    }
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

    DMA_Schedule(sys, &sys->DMA7);
}

void DMA9_Enable(struct Console* sys, struct DMA_Channel* channel, u8 channel_id)
{
    channel->Latched_SrcAddr = channel->SrcAddr;
    channel->Latched_DstAddr = channel->DstAddr;
    channel->Latched_NumWords = (channel->CR.NumWords ? channel->CR.NumWords : 0x200000);

    switch(channel->CR.StartMode9)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
        sys->DMA9.ChannelTimestamps[channel_id] = sys->AHB9.Timestamp+1;
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
    case 5: // NTR Gamecard
    {
        channel->CurrentMode = DMAStart_NTRCard;
        break;
    }
    case 6: // AGB Gamepak
    {
        channel->CurrentMode = DMAStart_AGBPakIRQ;
        // TODO: how does this work?
        // is it even hooked up?
        break;
    }
    case 7: // 3D Command FIFO
    {
        channel->CurrentMode = DMAStart_3DFIFO;
        break;
    }
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
    DMA_Schedule(sys, &sys->DMA9);
}

void DMA_Run(struct Console* sys, struct DMA_Controller* cnt, u8 id, const bool a9)
{
    u32 rmask;
    u32 wmask;

    struct DMA_Channel* channel = &cnt->Channels[id];

    if (channel->CR.Width32)
    {
        rmask = wmask = u32_max;
    }
    else
    {
        rmask = ROR32(u16_max, (channel->Latched_SrcAddr & 2)*8);
        wmask = ROR32(u16_max, (channel->Latched_DstAddr & 2)*8);
    }
    bool rseq = false;
    bool wseq = false;
    while(channel->Latched_NumWords > 0)
    {
        if (a9)
        {
            if (!AHB9_NegOwnership(sys, &sys->DMA9.ChannelTimestamps[id], id, false))
            {
                rseq = false;
                wseq = false;
            }
        }
        timestamp time = cnt->ChannelTimestamps[id];
        u32 read;
        if (a9)
        {
            read = AHB9_Read(sys, &cnt->ChannelTimestamps[id], channel->Latched_SrcAddr, rmask, false, rseq, &rseq, true);
        }
        else
        {
            read = AHB7_Read(sys, &cnt->ChannelTimestamps[id], channel->Latched_SrcAddr, rmask, false, rseq, &rseq, true);
        }
        time = sys->DMA9.ChannelTimestamps[id] - time;

        channel->Latched_SrcAddr += channel->SrcInc;

        if (a9)
        {
            if (!AHB9_NegOwnership(sys, &cnt->ChannelTimestamps[id], id, false))
            {
                rseq = false;
                wseq = false;
            }
            AHB9_Write(sys, &cnt->ChannelTimestamps[id], channel->Latched_DstAddr, read, wmask, false, &wseq, true);
        }
        else
        {
            AHB7_Write(sys, &cnt->ChannelTimestamps[id], channel->Latched_DstAddr, read, wmask, false, &wseq, true);
        }
        // CHECKME: should this only apply to the actual first?
        if (!rseq)
        {
            // TODO: figure out why exactly this happens?
            if (time == 1)
                cnt->ChannelTimestamps[id] +=1;
        }

        rseq = (channel->SrcInc > 0);
        wseq = (channel->DstInc > 0);

        channel->Latched_DstAddr += channel->DstInc;

        channel->Latched_NumWords -= 1;

        if (!channel->CR.Width32)
        {
            rmask = ROR32(u16_max, (channel->Latched_SrcAddr & 2)*8);
            wmask = ROR32(u16_max, (channel->Latched_DstAddr & 2)*8);
        }
    }

    // end

    if (channel->CR.Repeat)
    {
        // reschedule
    }
    else
    {
        channel->CR.Enable = false;
        sys->DMA9.ChannelTimestamps[id] = timestamp_max;
    }

    if (channel->CR.IRQ) // idk
        ;

    DMA_Schedule(sys, cnt);
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
                DMA9_Enable(sys, cur, channel);
            }
            else
            {
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
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
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
}
