#include "dma.h"
#include "sound.h"
#include "../console.h"
#include "../scheduler.h"




timestamp DMA_GetNext(struct Console* sys, bool a9, const bool inclusive) 
{
    struct DMA_Controller* cnt = ((a9) ? &sys->DMA9 : &sys->DMA7);

    timestamp time = cnt->NextTime;
    int cur = stdc_trailing_zeros((u32)cnt->CurMask);
    if (inclusive && (time > cnt->ChannelTimestamps[cur])) 
                      time = cnt->ChannelTimestamps[cur];
    return time;
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
    struct DMA_Controller* cnt = ((a9) ? &sys->DMA9 : &sys->DMA7);

    timestamp time = timestamp_max;
    u8 id = 31; // returning an id of 31 makes it check the cur mask somewhere it should always be 0

    int max = stdc_trailing_zeros((u32)cnt->CurMask);
    #pragma unroll (DMA7_Max)
    for (int i = 0; i < max; i++)
    {
        if (time > cnt->ChannelTimestamps[i])
        {
            time = cnt->ChannelTimestamps[i];
            id = i;
        }
    }
    cnt->NextTime = time;
    cnt->NextID = id;
}

void StartDMA9(struct Console* sys, timestamp start, u8 mode)
{
    bool update = false;
    for (int i = 0; i < 4; i++)
    {
        if (!sys->DMA9.Channels[i].CR.Enable) continue;
        if (sys->DMA9.Channels[i].CurrentMode != mode) continue;
        // checkme: starting dma while already started?
        if (sys->DMA9.CurMask & (1<<i))
        {
            // CHECKME
            if ((sys->DMA9.Channels[i].CurrentMode != DMAStart_NTRCard) && (sys->DMA9.Channels[i].CurrentMode != DMAStart_3DFIFO))
            {
                LogPrint(LOG_DMA|LOG_ODD, "DMA9: channel already going??? mask:%X errchan:%i chanmode:%i\n", sys->DMA9.CurMask, i, sys->DMA9.Channels[i].CurrentMode);
            }
            continue;
        }
        sys->DMA9.ChannelTimestamps[i] = start;
        update = true;
    }
    if (update) DMA_Schedule(sys, true);
}

void StartDMA7(struct Console* sys, timestamp start, u8 mode)
{
    bool update = false;
    for (int i = DMA7_NormalBase; i < DMA7_NormalMax; i++)
    {
        if (!sys->DMA7.Channels[i].CR.Enable) continue;
        if (sys->DMA7.Channels[i].CurrentMode != mode) continue;
        // checkme: starting dma while already started?
        if (sys->DMA7.CurMask & (1<<i))
        {
            // CHECKME
            if (sys->DMA7.Channels[i].CurrentMode != DMAStart_NTRCard)
            {
                LogPrint(LOG_DMA|LOG_ODD, "DMA7: channel already going??? mask:%X errchan:%i chanmode:%i\n", sys->DMA7.CurMask, i, sys->DMA7.Channels[i].CurrentMode);
            }
            continue;
        }
        sys->DMA7.ChannelTimestamps[i] = start;
        update = true;
    }
    if (update) DMA_Schedule(sys, false);
}

void StartSoundDMA(struct Console* sys, u8 id, timestamp start, bool matters)
{
    if (!sys->DMA7.Channels[id].CR.Enable) return;
    if (sys->DMA7.ChannelTimestamps[id] != timestamp_max)
    {
        if (matters) LogPrint(LOG_SOUND, "Starting sound dma while active\n");
        return; // active
    }
    sys->DMA7.ChannelTimestamps[id] = start;
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
        // checkme: this probably works.
        if (sys->GCROMCR[false].DataReady)
            StartDMA7(sys, sys->AHB7.Timestamp+1, DMAStart_NTRCard); // checkme: delay?
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
        // checkme: this probably works.
        if (sys->GCROMCR[true].DataReady)
            StartDMA9(sys, sys->AHB9.Timestamp+1, DMAStart_NTRCard); // checkme: delay?
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

    if (channel->Latched_NumWords == 0)
    {
        // CHECKME: idk, where and when things are latched needs testing.
        if (channel->CR.SourceCR == 3) channel->Latched_SrcAddr = channel->SrcAddr;
        if (channel->CR.DestCR   == 3) channel->Latched_DstAddr = channel->DstAddr;

        channel->Latched_NumWords = channel->NumWords;
    }


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
    if (channel->CurrentMode == DMAStart_Audio)
    {
        if (numword > 4) numword = 4; // checkme
    }


    while(numword > 0)
    {
        if (!channel->CR.Width32)
        {
            rmask = ROR32(u16_max, (channel->Latched_SrcAddr & 2)*8);
            wmask = ROR32(u16_max, (channel->Latched_DstAddr & 2)*8);
        }
        //if (channel->CurrentMode == DMAStart_HBlank)
            //printf("dmatimea: cw%i lw%i nm%i md%i ad:%08X ti%li\n", numword, channel->Latched_NumWords, id, channel->CurrentMode, channel->Latched_SrcAddr, cnt->ChannelTimestamps[id]);

        if (!AHB_NegOwnership(sys, &cnt->ChannelTimestamps[id], false, a9))
        {
            rseq = false;
            wseq = false;
            tseq = false; // checkme
        }
        channel->Latched_SrcAddr &= channel->SrcAddrMask;

        //if (channel->CurrentMode == DMAStart_HBlank) printf("dmatimeb: cw%i lw%i nm%i md%i ti%li\n", numword, channel->Latched_NumWords, id, channel->CurrentMode, cnt->ChannelTimestamps[id]);
        //if (a9) printf("dmatimeb: %i %li\n", id, cnt->ChannelTimestamps[id]);
        timestamp diff = cnt->ChannelTimestamps[id];
        u32 read;
        if (a9)
        {
            read = AHB9_Read(sys, &cnt->ChannelTimestamps[id], channel->Latched_SrcAddr, rmask, false, true, &rseq, true);
        }
        else
        {
            read = AHB7_Read(sys, &cnt->ChannelTimestamps[id], channel->Latched_SrcAddr, rmask, false, true, &rseq, (channel->CurrentMode != DMAStart_Audio), 0xFFFFFFFF /*checkme?*/);
        }
        diff = cnt->ChannelTimestamps[id] - diff;
        channel->Latched_SrcAddr += channel->SrcInc;
        //if (a9) printf("dmatimec: %i %li\n", id, cnt->ChannelTimestamps[id]);
        //if (channel->CurrentMode == DMAStart_HBlank) printf("dmatimec: cw%i lw%i nm%i md%i ti%li\n", numword, channel->Latched_NumWords, id, channel->CurrentMode, cnt->ChannelTimestamps[id]);

        if (channel->CurrentMode != DMAStart_Audio)
        {
            if (!AHB_NegOwnership(sys, &cnt->ChannelTimestamps[id], false, a9))
            {
                rseq = false;
                wseq = false;
                tseq = false; // checkme
            }
            //if (channel->CurrentMode == DMAStart_HBlank) printf("dmatimed: cw%i lw%i nm%i md%i ti%li\n", numword, channel->Latched_NumWords, id, channel->CurrentMode, cnt->ChannelTimestamps[id]);
            //if (a9) printf("dmatimed: %i %li\n", id, cnt->ChannelTimestamps[id]);
            channel->Latched_DstAddr &= channel->DstAddrMask;
            if (a9)
            {
                AHB9_Write(sys, &cnt->ChannelTimestamps[id], channel->Latched_DstAddr, read, wmask, false, &wseq, true);
            }
            else
            {
                AHB7_Write(sys, &cnt->ChannelTimestamps[id], channel->Latched_DstAddr, read, wmask, false, &wseq, true, 0xFFFFFFFF /*checkme?*/);
            }
            channel->Latched_DstAddr += channel->DstInc;
        }
        else
        {
            cnt->ChannelTimestamps[id] += 1;
            SoundFIFO_Fill(sys, read, id, cnt->ChannelTimestamps[id]);
        }
        // CHECKME: should this only apply to the actual first?
        if (!tseq)
        {
            // TODO: figure out why exactly this happens?
            if (diff == 1)
                cnt->ChannelTimestamps[id] += 1;
        }
        tseq = true;

        rseq = (channel->SrcInc > 0);
        wseq = (channel->DstInc > 0);

        channel->Latched_NumWords -= 1;
        numword -= 1;
    }

    //Bus_MainRAM_ReleaseHold(sys, a9 ? &sys->AHB9 : &sys->AHB7); why isn't this necessary? (was this due to dma scheduling bugs?)

    // end

    cnt->CurMask &= ~1<<id;

    bool dmaqueued = false;
    if (channel->Latched_NumWords == 0)
    {
        if (channel->CR.Repeat && (channel->CurrentMode != DMAStart_Immediate /*checkme?*/))
        {
            if (channel->CurrentMode == DMAStart_NTRCard)
            {
                if ((sys->ExtMemCR_Shared.NDSCardAccess == !a9) && sys->GCROMCR[a9].DataReady)
                {
                    dmaqueued = true;
                }
            }
            // TODO: reschedule
            // CHECKME: does this even matter anymore? i dont think it does.
            //LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: DMA WANTS RESCHEDULE %i\n", channel->CurrentMode);
        }
        else
        {
            channel->CR.Enable = false;
        }

        if ((channel->Latched_NumWords == 0) && channel->CR.IRQ)
            Console_ScheduleIRQs(sys, IRQ_DMA0+id, a9, cnt->ChannelTimestamps[id]); // checkme: delay
    }
    else if (channel->CurrentMode == DMAStart_3DFIFO)
    {
        if (sys->GX3D.Status.FIFOHalfEmpty)
            dmaqueued = true;
    }

    if ((channel->CurrentMode == DMAStart_Audio) && (sys->SoundChannels[id].FIFO_Bytes <= 16) && channel->CR.Enable)
    {
        dmaqueued = true;
    }

    if (!dmaqueued)
        cnt->ChannelTimestamps[id] = timestamp_max;

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
        cur->NumWords = (cur->CR.NumWords ? cur->CR.NumWords : 0x200000);

        if (oldcr.Enable ^ cur->CR.Enable)
        {
            if (cur->CR.Enable == true)
            {
                // starting dma channel
                DMA9_Enable(sys, cur);
            }
            else
            {
                //LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: Stopping DMA9\n");
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
        cur->NumWords = (cur->CR.NumWords ? cur->CR.NumWords : 0x200000);

        if (oldcr.Enable ^ cur->CR.Enable)
        {
            if (cur->CR.Enable == true)
            {
                // starting dma channel
                DMA7_Enable(sys, cur, channel);
            }
            else
            {
                //LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: Stopping DMA7\n");
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
}
