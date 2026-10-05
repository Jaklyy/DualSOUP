#include "dma.h"
#include "core/bus/bus.h"
#include "core/utils.h"
#include "sound.h"
#include "core/console.h"
#include "core/scheduler.h"


void DMA_Init(Console* sys)
{
    for (u32 i = DMA7_SoundBase; i < DMA7_SoundMax; i++)
    {
        sys->DMA7[i].CurrentMode = DMAStart_Audio;
        sys->DMA7[i].CR.Width32 = true;
        sys->DMA7[i].Latched_Width32 = true;
        sys->DMA7[i].SrcInc = 4;
        sys->DMA7[i].SrcAddrMask = 0x07FFFFFC;
    }
    for (u32 i = DMA7_SoundCapBase; i < DMA7_SoundCapMax; i++)
    {
        sys->DMA7[i].CurrentMode = DMAStart_AudioCap;
        sys->DMA7[i].CR.Width32 = true;
        sys->DMA7[i].Latched_Width32 = true;
        sys->DMA7[i].DstInc = 4;
        sys->DMA7[i].CR.DestCR = 3;
        sys->DMA7[i].DstAddrMask = 0x07FFFFFC;
        sys->DMA7[i].CR.Enable = true;
        sys->DMA7[i].CR.Repeat = true;
    }

    for (u32 i = DMA7_NormalBase; i < DMA7_NormalMax; i++)
    {
        sys->DMA7[i].SrcAddrMask = (i == 0) ? 0x07FFFFFE : 0x0FFFFFFE;
        sys->DMA7[i].DstAddrMask = (i == 3) ? 0x07FFFFFE : 0x0FFFFFFE;
    }

    for (u32 i = DMA9_NormalBase; i < DMA9_NormalMax; i++)
    {
        sys->DMA9[i].SrcAddrMask = 0x0FFFFFFE;
        sys->DMA9[i].DstAddrMask = 0x0FFFFFFE;
    }
}

void StartDMA9(Console* sys, timestamp start, u8 mode)
{
    for (u64 i = DMA9_NormalBase; i < DMA9_NormalMax; i++)
    {
        if (!sys->DMA9[i].CR.Enable) continue;
        if (sys->DMA9[i].CurrentMode != mode) continue;
        // checkme: starting dma while already started?
        if (sys->DMA9[i].WriteCur != sys->DMA9[i].BurstMax)
        {
            // CHECKME
            //if ((sys->DMA9[i].CurrentMode != DMAStart_NTRCard) && (sys->DMA9[i].CurrentMode != DMAStart_3DFIFO))
            {
                LogPrint(LOG_DMA|LOG_ODD, "DMA9: channel already going??? errchan:%"PRIu64" chanmode:%"PRIu8"\n", i, sys->DMA9[i].CurrentMode);
            }
            continue;
        }
        sys->DMA9[i].NeedsInit = true;
        Sched_AddEvent(sys, start, Evt_DMA90 + i);
    }
}

void StartDMA7(Console* sys, timestamp start, u8 mode)
{
    for (u64 i = DMA7_NormalBase; i < DMA7_NormalMax; i++)
    {
        DMA_Channel* cur = &sys->DMA7[i];
        if (!cur->CR.Enable) continue;
        if (cur->CurrentMode != mode) continue;
        // checkme: starting dma while already started?
        if (cur->WriteCur != cur->BurstMax)
        {
            // CHECKME
            //if (sys->DMA7[i].CurrentMode != DMAStart_NTRCard)
            {
                LogPrint(LOG_DMA|LOG_ODD, "DMA7: channel already going??? errchan:%"PRIu64" chanmode:%"PRIu8"\n", i, sys->DMA7[i].CurrentMode);
            }
            continue;
        }
        cur->NeedsInit = true;
        Sched_AddEvent(sys, start, Evt_DMA70 + (i-DMA7_NormalBase));
    }
}

void StartSoundCapDMA(Console* sys, u8 id, timestamp start)
{
    if (!sys->DMA7[id+DMA7_SoundCapBase].CR.Enable) return;
    sys->DMA7[id+DMA7_SoundCapBase].NeedsInit = true;
    Sched_AddEvent(sys, start, Evt_SCapDMA70+id);
}

void StartSoundDMA(Console* sys, u8 id, timestamp start)
{
    if (!sys->DMA7[id+DMA7_SoundBase].CR.Enable) return;
    if (sys->DMA7[id+DMA7_SoundBase].WriteCur != sys->DMA7[id+DMA7_SoundBase].BurstMax)
    {
        // CHECKME
        //if (sys->DMA7[i].CurrentMode != DMAStart_NTRCard)
        {
            LogPrint(LOG_DMA|LOG_ODD, "DMA SND: channel already going??? errchan:%"PRIu8" chanmode:%"PRIu8"\n", id, sys->DMA7[id+DMA7_SoundBase].CurrentMode);
        }
        return;
    }
    sys->DMA7[id+DMA7_SoundBase].NeedsInit = true;
    Sched_AddEvent(sys, start, Evt_SndDMA70+id);
}

void DMA7_Enable(Console* sys, DMA_Channel* channel, timestamp now)
{
    channel->Latched_SrcAddr = channel->SrcAddr;
    channel->Latched_DstAddr = channel->DstAddr;
    channel->Latched_NumWords = 0;

    switch(channel->CR.StartMode7)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
        StartDMA7(sys, now, DMAStart_Immediate);
        break;
    }
    case 1: // VBlank
    {
        channel->CurrentMode = DMAStart_VBlank;
        break;
    }
    case 2: // NTR Game Card
    {
        channel->CurrentMode = DMAStart_NTRCard;
        // checkme: this probably works.
        if (sys->GCROMCR[false].DataReady)
            StartDMA7(sys, now, DMAStart_NTRCard); // checkme: delay?
        break;
    }
    #if 0
    case 3: // (DMA 0 & 2) WiFi IRQ / (DMA 1 & 3) AGB Game Pak IRQ
    {
        channel->CurrentMode = (channel_id & 0b01) ? DMAStart_AGBPakIRQ : DMAStart_WiFiIRQ;
        // agb pak dma is probably based on the pak slot irq pin?
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

void DMA9_Enable(Console* sys, DMA_Channel* channel, timestamp now)
{
    channel->Latched_SrcAddr = channel->SrcAddr;
    channel->Latched_DstAddr = channel->DstAddr;
    channel->Latched_NumWords = 0;

    switch(channel->CR.StartMode9)
    {
    case 0: // Immediate
    {
        channel->CurrentMode = DMAStart_Immediate;
        StartDMA9(sys, now, DMAStart_Immediate);
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
    case 5: // NTR Game Card
    {
        channel->CurrentMode = DMAStart_NTRCard;
        // checkme: this probably works.
        if (sys->GCROMCR[true].DataReady)
            StartDMA9(sys, now, DMAStart_NTRCard); // checkme: delay?
        break;
    }
    case 7: // 3D Command FIFO
    {
        channel->CurrentMode = DMAStart_3DFIFO;

        if (sys->GX3D.Status.FIFOHalfEmpty)
            StartDMA9(sys, now, DMAStart_3DFIFO);
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
    case 6: // AGB Game Pak
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

void DMA_CompPost(Console* sys, timestamp now, const u8 id, u32 rdata, const bool load, const bool a9)
{
    DMA_Channel* channel = (a9 ? &sys->DMA9[id] : &sys->DMA7[id]);

    channel->CompCur++;
    if (channel->CompCur == channel->CompMax) // burst complete
    {
        bool dmaqueued = false;
        if (channel->Latched_NumWords <= 0)
        {
            if (channel->CR.Repeat && (channel->CurrentMode != DMAStart_Immediate /*checkme?*/))
            {
                if ((channel->CurrentMode == DMAStart_NTRCard)
                && (sys->ExtMemCR_Shared.NDSCardA7Access == !a9)
                && sys->GCROMCR[a9].DataReady)
                    dmaqueued = true;
            }
            else channel->CR.Enable = false;

            if (channel->CR.IRQ)
                Sched_AddEvent(sys, now, (a9 ? (Evt_IRQ9_DMA0 + id) : (Evt_IRQ7_DMA0 + (id-DMA7_NormalBase)))); // checkme: delay
        }
        else if (channel->CurrentMode == DMAStart_3DFIFO)
        {
            if (sys->GX3D.Status.FIFOHalfEmpty)
                dmaqueued = true;
        }

        if ((channel->CurrentMode == DMAStart_Audio) && (sys->SoundChannels[id-DMA7_SoundBase].FIFO_Bytes <= 16) && channel->CR.Enable)
            dmaqueued = true;

        if (dmaqueued)
        {
            channel->NeedsInit = true;
            Sched_AddEvent(sys, now, Evt_SCapDMA70 + id);
        }
    }

    if (!load)
    {
        if (channel->CurrentMode == DMAStart_Audio) SoundFIFO_Fill(sys, channel->RData, id-DMA7_SoundBase); // spaghetti...
        return;
    }

    if (channel->CurrentMode == DMAStart_AudioCap)
    {
        channel->RData = sys->SoundCaptures[id-DMA7_SoundCapBase].FIFO.Raw;
        sys->SoundCaptures[id-DMA7_SoundCapBase].Flush = false;
    }
    else
    {
        channel->RData = rdata;
        if (!channel->Latched_Width32)
        {
            channel->RData = ROR32(channel->RData , (((channel->Latched_SrcAddr - channel->SrcInc) & 2) * 8));
            channel->RData &= 0xFFFF;
            channel->RData |= channel->RData << 16;
        }
    }
}

void DMA_Step(Console* sys, const u8 id, timestamp now, const bool a9)
{
    DMA_Channel* channel = (a9 ? &sys->DMA9[id] : &sys->DMA7[id]);

    if (!channel->NeedsInit && (channel->WriteCur == channel->BurstMax)) return;

    if (channel->NeedsInit)
    {
        if (channel->Latched_NumWords <= 0)
        {
            // CHECKME: idk, where and when things are latched needs testing.
            if (channel->CR.SourceCR == 3) channel->Latched_SrcAddr = channel->SrcAddr;
            if (channel->CR.DestCR   == 3) channel->Latched_DstAddr = channel->DstAddr;

            channel->Latched_NumWords = channel->NumWords;
            channel->Latched_Width32 = channel->CR.Width32; // idk
        }

        if (channel->WriteCur == channel->BurstMax) // burst complete
        {
            channel->BurstMax = channel->Latched_NumWords;
            if (channel->CurrentMode == DMAStart_3DFIFO)
                DS_CLAMP(channel->BurstMax, >, 112)
            if (channel->CurrentMode == DMAStart_Audio)
                DS_CLAMP(channel->BurstMax, >, 4)
            if (channel->CurrentMode == DMAStart_AudioCap)
                DS_CLAMP(channel->BurstMax, >, 1)

            channel->ReadCur = 0;
            channel->WriteCur = 0;
            channel->CompCur = 0;
            channel->CompMax = channel->BurstMax*2;
        }
        channel->NeedsInit = false;
    }

    BusReq req;
#if 0
    if (channel->DoBusy)
    {
        req = (BusReq){
            .Addr = channel->Latched_DstAddr, // guess
            .WrData = 0,
            .Write = false,
            .Lock = false,
            .Man9 = MAN9_DMA0+id,
            .Prot = {
                .Data = true,
                .Privileged = false,
                .Bufferable = false,
                .Cacheable = false,
            },
            .Size = (channel->Latched_Width32 ? HSIZE_32 : HSIZE_16),
            .Type = HTRANS_BUSY,
            .CB = CB9_DMA,
        };
        channel->DoBusy = false;
    }
    else 
#endif
    if (channel->ReadCur == channel->WriteCur) // read
    {
        if (channel->CurrentMode == DMAStart_AudioCap)
        {
            req = (BusReq){
                .Addr = channel->Latched_DstAddr, // idk
                .WrData = 0,
                .Write = false,
                .Lock = false,
                .Man = MAN7_SCAPDMA0 + id,
                .Prot = {
                    .Data = true,
                    .Privileged = false,
                    .Bufferable = false,
                    .Cacheable = false,
                },
                .Size = (channel->Latched_Width32 ? HSIZE_32 : HSIZE_16),
                .Type = HTRANS_BUSY, // checkme: complete guess
                .CB = CB7_DMA,
            };

            //channel->RData = sys->SoundCaptures[id-DMA7_SoundCapBase].FIFO.Raw;
            //sys->SoundCaptures[id-DMA7_SoundCapBase].Flush = false;
        }
        else
        {
            req = (BusReq){
                .Addr = channel->Latched_SrcAddr,
                .WrData = 0,
                .Write = false,
                .Lock = false,
                .Man = (a9 ? MAN9_DMA0 : MAN7_SCAPDMA0) + id,
                .Prot = {
                    .Data = true,
                    .Privileged = false,
                    .Bufferable = false,
                    .Cacheable = false,
                },
                .Size = (channel->Latched_Width32 ? HSIZE_32 : HSIZE_16),
                .Type = (((channel->ReadCur == 0) || channel->SrcInc <= 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
                .CB = (a9 ? CB9_DMA : CB7_DMA),
            };
            channel->Latched_SrcAddr = (channel->Latched_SrcAddr + channel->SrcInc) & channel->SrcAddrMask;
        }
        channel->ReadCur++;

        //if (channel->ReadCur == 0) channel->DoBusy = true; // idk
    }
    else
    {
        if (channel->CurrentMode == DMAStart_Audio)
        {
            req = (BusReq){
                .Addr = channel->Latched_SrcAddr, // idk
                .WrData = 0,
                .Write = true,
                .Lock = false,
                .Man = MAN7_SCAPDMA0 + id,
                .Prot = {
                    .Data = true,
                    .Privileged = false,
                    .Bufferable = false,
                    .Cacheable = false,
                },
                .Size = (channel->Latched_Width32 ? HSIZE_32 : HSIZE_16),
                .Type = HTRANS_BUSY, // checkme: complete guess
                .CB = CB7_DMA,
            };
        }
        else
        {
            req = (BusReq){
                .Addr = channel->Latched_DstAddr,
                .WrData = (channel->CurrentMode == DMAStart_AudioCap) ? channel->RData : 0, // this is gonna get hacky as fuck
                .Write = true,
                .Lock = false,
                .Man = (a9 ? MAN9_DMA0 : MAN7_SCAPDMA0) + id,
                .Prot = {
                    .Data = true,
                    .Privileged = false,
                    .Bufferable = false,
                    .Cacheable = false,
                },
                .Size = (channel->Latched_Width32 ? HSIZE_32 : HSIZE_16),
                .Type = ((channel->WriteCur == 0 || channel->DstInc <= 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
                .CB = (a9 ? CB9_DMA : CB7_DMA),
            };
            channel->Latched_DstAddr = (channel->Latched_DstAddr + channel->DstInc) & channel->DstAddrMask;
        }
        channel->WriteCur++;
        channel->Latched_NumWords--;
    }
    Bus_Req(sys, &req, now, a9);
}

u32 DMA_IOReadHandler(DMA_Channel* channels, u32 addr)
{
    addr &= 0xFC;
    addr -= 0xB0;
    int channel = (addr / 4) / 3;
    int reg = (addr / 4) % 3;

    DMA_Channel* cur = &channels[channel];

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

void DMA9_IOWriteHandler(Console* sys, timestamp now, DMA_Channel* channels, u32 addr, u32 val, u32 mask)
{
    addr &= 0xFC;
    addr -= 0xB0;
    int channel = (addr / 4) / 3;
    int reg = (addr / 4) % 3;

    DMA_Channel* cur = &channels[channel];

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
                DMA9_Enable(sys, cur, now);
            }
            else
            {
                cur->WriteCur = 0;
                cur->BurstMax = 0;
                //LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: Stopping DMA9\n");
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
}

void DMA7_IOWriteHandler(Console* sys, timestamp now, DMA_Channel* channels, u32 addr, u32 val, const u32 mask)
{
    addr &= 0xFC;
    addr -= 0xB0;
    int channel = (addr / 4) / 3;
    int reg = (addr / 4) % 3;

    DMA_Channel* cur = &channels[channel];

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
        cur->NumWords = (cur->CR.NumWords ? cur->CR.NumWords : ((channel == 3) ? 0x10000 : 0x4000));

        if (oldcr.Enable ^ cur->CR.Enable)
        {
            if (cur->CR.Enable == true)
            {
                // starting dma channel
                DMA7_Enable(sys, cur, now);
            }
            else
            {
                cur->WriteCur = 0;
                cur->BurstMax = 0;
                //LogPrint(LOG_UNIMP | LOG_DMA, "UNIMP: Stopping DMA7\n");
                // stopping dma channel
                // TODO: allegedly under specific circumstances this can lock up the bus?
            }
        }
        break;
    }
    }
}
