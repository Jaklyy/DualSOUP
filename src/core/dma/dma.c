#include "dma.h"
#include "../console.h"




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

timestamp DMA_TimeNextScheduled(const timestamp* ts, const unsigned numtst)
{
    timestamp ret = timestamp_max;

    for (unsigned i = 0; i < numtst; i++)
    {
        if (ret > ts[i])
        {
            ret = ts[i];
        }
    }
    return ret;
}

void DMA7_Enable(struct DMA_Channel* channel, u8 channel_id)
{
    channel->Latched_SrcAddr = channel->Latched_SrcAddr;
    channel->Latched_DstAddr = channel->Latched_SrcAddr;
    channel->Latched_SrcAddr = channel->Latched_SrcAddr;

    switch(channel->CR.StartMode7)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
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
        channel->CurrentMode = (channel_id & 0b01) ? DMAStart_AGBCartIRQ : DMAStart_WiFiIRQ;
        // agb cart dma is probably based on the cart slot irq pin?
        // wifi irq might not actually be real?
        break;
    }
    }
}

void DMA9_Enable(struct DMA_Channel* channel)
{
    switch(channel->CR.StartMode9)
    {
    case 0: // Immediate
    {
        break;
    }
    case 1: // VBlank
    {
        break;
    }
    case 2: // HBlank (excl. VBlank)
    {
        break;
    }
    case 3: // synchronize to start of display(??)
    {
        // not sure what this means actually...?
        // melonds seems to run it once per vcount for vcounts 2 - 193; and then explicitly tries to stop them on vcount 194?
        break;
    }
    case 4: // Display FIFO
    {
        break;
    }
    case 5: // NTR Gamecard
    {
        break;
    }
    case 6: // AGB Cartridge
    {
        // TODO: how does this work?
        // is it even hooked up?
        break;
    }
    case 7: // 3D Command FIFO
    {
        break;
    }
    }
}

void DMA_Run(struct Console* sys, struct DMA_Channel* channel, u8 id)
{
    if (channel->CR.Width32)
    {

    }
    while(channel->Latched_NumWords > 0)
    {
        AHB9_NegOwnership(sys, &sys->AHB9.Timestamp, id, false);
        //Bus9Read
        AHB9_NegOwnership(sys, &sys->AHB9.Timestamp, id, false);
        //Bus9Write
    }

    // end

    if (!channel->CR.Repeat)
    {
        // reschedule
    }
    else
    {
        channel->CR.Enable = false;
    }

    if (channel->CR.IRQ) // idk
        ;

}

void DMA9_IOWriteHandler(struct DMA_Channel* channels, u32 address, u32 val, u32 mask)
{
    address &= 0xFF;
    address -= 0xB0;
    int channel = (address / 4) % 4;
    int reg = (address / 4) % 3;

    struct DMA_Channel* cur = &channels[channel];

    switch(reg)
    {
    case 0: // source address
    {
        val &= 0x0FFFFFFE;

        MaskedWrite(cur->SrcAddr, val, mask);
        break;
    }
    case 1: // destination address
    {
        val &= 0x0FFFFFFE;

        MaskedWrite(cur->DstAddr, val, mask);
        break;
    }
    case 2: // control register
    {

    }
    }
}

void DMA7_IOWriteHandler(struct DMA_Channel* channels, u32 addr, u32 val, const u32 mask)
{
    addr &= 0xFF;
    addr -= 0xB0;
    int channel = (addr / 4) % 4;
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
                DMA7_Enable(cur, channel);
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
