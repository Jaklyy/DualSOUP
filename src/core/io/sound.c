#include <stdckdint.h>
#include "../console.h"
#include "sound.h"
#include "dma.h"
#include "timer.h"



extern void Timer7_UpdateCRs(struct Console* sys, timestamp now);
void SoundChannel_Disable(struct Console* sys, const u8 id, const timestamp now, const bool bushogged)
{
    Scheduler_RunEventManual(sys, now, Evt_Timer7, false, bushogged);

    // disable dma
    sys->DMA7.Channels[id].CR.Repeat = false;
    sys->DMA7.Channels[id].CR.Enable = false;

    // disable timer
    sys->Timers7[id+4].NeedsUpdate = true;
    sys->Timers7[id+4].BufferedRegs = 0x00'0000;

    sys->SoundChannels[id].CR.Enable = false;
    //printf("channel stop %i %i\n", id, sys->SoundChannels[id].CR.RepeatMode);

    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
}

void SoundChannel_KillAll(struct Console* sys, const timestamp now)
{
    //printf("killing sound channels\n");
    Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
    for (int i = 0; i < 16; i++)
    {
        // disable dma
        sys->DMA7.Channels[i].CR.Repeat = false;
        sys->DMA7.Channels[i].CR.Enable = false;

        // disable timer
        sys->Timers7[i+4].NeedsUpdate = true;
        sys->Timers7[i+4].BufferedRegs = 0x00'0000;
    }

    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
}

void SoundChannel_Start(struct Console* sys, SoundChannel* channel, const u8 id, const timestamp now)
{
    //printf("starting %i manually\n", id);
    if (channel->CR.Format != AudioFormat_PSGNoise) // checkme
    {
        // set up DMA
        //channel->CR.RepeatMode = 2;
        sys->DMA7.Channels[id].Latched_NumWords = channel->SoundLen + channel->LoopOffs;
        sys->DMA7.Channels[id].NumWords = channel->SoundLen;
        sys->DMA7.Channels[id].SrcAddr = channel->SrcAddr;
        sys->DMA7.Channels[id].SrcAddrReload = channel->SrcAddr + (channel->LoopOffs*4);
        sys->DMA7.Channels[id].CR.SourceCR = (channel->CR.RepeatMode & 1) ? 3 : 1;
        sys->DMA7.Channels[id].CR.Repeat = !(channel->CR.RepeatMode & 2);
        sys->DMA7.Channels[id].CR.Enable = true;
        StartSoundDMA(sys, id, now+1);
    }

    // set up timers
    Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
    sys->Timers7[id+4].NeedsUpdate = true;
    sys->Timers7[id+4].BufferedRegs = 0xC0'0000 /* Enable, IRQ */ | channel->Timer;
    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);

    if ((channel->CR.RepeatMode & 2))// && (channel->CR.Format != AudioFormat_PSGNoise))
    {
        switch(channel->CR.Format)
        {
        case AudioFormat_PCM8:
            channel->SampleMax = ((channel->SoundLen + channel->LoopOffs)*4) + 3;
            break;
        case AudioFormat_PCM16:
            channel->SampleMax = ((channel->SoundLen + channel->LoopOffs)*2) + 3;
            break;
        case AudioFormat_ADPCM:
            channel->SampleMax = 1 + ((channel->SoundLen + channel->LoopOffs-1)*8) + 11;
            break;
        case AudioFormat_PSGNoise:
            channel->SampleMax = u64_max;
            break;
        }
    }
    else channel->SampleMax = u64_max;

    // reset fifo
    channel->FIFOSatiated = false;
    channel->FIFODrainPtr = 0;
    channel->FIFOFillPtr = 0;
    channel->Prog = 0;
}

void SoundChannel_TryStartAll(struct Console* sys, const timestamp now)
{
    for (int i = 0; i < 16; i++)
    {
        if (sys->SoundChannels[i].CR.Enable)
        {
            SoundChannel_Start(sys, &sys->SoundChannels[i], i, now);
        }
    }
}

void SoundFIFO_Fill(struct Console* sys, const u32 val, const u8 id, const timestamp now)
{
    //printf("try fill %i\n", id);
    //Scheduler_RunEventManual(sys, now, Evt_Timer7, false, false);
    SoundChannel* channel = &sys->SoundChannels[id];
    MemoryWrite(32, channel->FIFO, channel->FIFOFillPtr, sizeof(channel->FIFO), val, 0xFFFFFFFF);
    channel->FIFOFillPtr+=4;
    channel->FIFOFillPtr &= 0x1F;
    if (((channel->FIFOFillPtr - channel->FIFODrainPtr) & 0x1F) > 16) channel->FIFOSatiated = true;
}

u32 SoundFIFO_Drain(struct Console* sys, SoundChannel* channel, u8 NumBytes, const u8 id, const timestamp now)
{
    if (!channel->FIFOSatiated && (channel->FIFOFillPtr == channel->FIFODrainPtr)) // fifo empty
    {
        LogPrint(LOG_SOUND|LOG_UNIMP, "SOUND FIFO OVERFLOW: Channel: %i cr:%08X p:%X m:%lX dma:%08X tim:%06X\n",
            id, channel->CR.Raw, channel->Prog, channel->SampleMax, sys->DMA7.Channels[id].CR.Raw, sys->Timers7[id+4].Regs);
        return 0;
    }
    u32 ret;
    switch(NumBytes)
    {
        case 1: ret = MemoryRead(8, channel->FIFO, channel->FIFODrainPtr, sizeof(channel->FIFO)); break;
        case 2: ret = MemoryRead(16, channel->FIFO, channel->FIFODrainPtr, sizeof(channel->FIFO)); break;
        case 4: ret = MemoryRead(32, channel->FIFO, channel->FIFODrainPtr, sizeof(channel->FIFO)); break;
        default: unreachable(); // if you do this you stupid
    }
    channel->FIFODrainPtr+=NumBytes;
    channel->FIFODrainPtr &= 0x1F;
    if (((channel->FIFOFillPtr - channel->FIFODrainPtr) & 0x1F) <= 16)
    {
        channel->FIFOSatiated = false;
        StartSoundDMA(sys, id, now+1);
    }
    return ret;
}

void SoundFIFO_Sample(struct Console* sys, const u8 id, const timestamp now)
{
    //printf("try sample %i %lu\n", id, now);
    SoundChannel* channel = &sys->SoundChannels[id];
    switch(channel->CR.Format)
    {
    case AudioFormat_PCM8:
    {
        if (channel->Prog >= 3) // fetch pcm
        {
            SoundFIFO_Drain(sys, channel, 1, id, now);
        }
        break;
    }
    case AudioFormat_PCM16:
    {
        if (channel->Prog >= 3) // fetch pcm
            SoundFIFO_Drain(sys, channel, 2, id, now);
        break;
    }
    case AudioFormat_ADPCM:
    {
        if (channel->Prog >= 12) // pcm
        {
            if (!(channel->Prog & 1))
                SoundFIFO_Drain(sys, channel, 1, id, now);
        }
        else if (channel->Prog == 8) // fetch header
        {
            SoundFIFO_Drain(sys, channel, 4, id, now);
        }
        break;
    }
    case AudioFormat_PSGNoise:
    {
        // TODO
        break;
    }
    }

    u32 tmp;
    if (!ckd_add(&tmp, channel->Prog, 1)) channel->Prog = tmp;

    //printf("%i %08X %08X %08X %08X %i %lu\n", id, channel->Prog, channel->SampleMax, sys->Timers7[id+4].Counter, sys->Timers7[id+4].Reload, sys->Timers7[id+4].Regs, now);

    if (channel->Prog >= channel->SampleMax)
    {
        //printf("stopping %i automatically\n", id);
        SoundChannel_Disable(sys, id, now, false);
    }
}

u32 SoundChannel_IORead(struct Console* sys, const u32 addr, const timestamp now)
{
    //printf("%08X\n", addr);
    if ((addr & 0xF) != 0) return 0; // checkme: supposedly only each channel's control reg can be read?
    u8 id = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[id];

    //printf("%i %i %i\n", id, channel->CR.Enable, channel->CR.RepeatMode);
    return channel->CR.Raw & 0x7FFFFFFF;
}

void SoundChannel_IOWrite(struct Console* sys, const u32 addr, const u32 val, const u32 mask, const timestamp now)
{
    u8 id = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[id];

    switch(addr & 0xC)
    {
    case 0x0:
        //if (id == 1 || id == 3)printf("write cr: %i %08X %08X\n", id, val, mask);
        u32 oldcr = channel->CR.Raw;
        MaskedWrite(channel->CR.Raw, val, mask & 0xFF7F837F);
        if (channel->CR.Enable && (oldcr>>31) && ((oldcr >> 24) ^ (channel->CR.Raw >> 24))) LogPrint(LOG_SOUND|LOG_UNIMP, "Updating sound CR while active %i\n", id);
        if (channel->CR.Enable ^ (oldcr>>31))
        {
            if (channel->CR.Enable)
            {
                SoundChannel_Start(sys, channel, id, now);
            }
            else //if (!channel->CR.Enable && (oldcr>>31))
            {
                //printf("stopping %i manually\n", id);
                SoundChannel_Disable(sys, id, now, true);
            }
        }
        break;

    case 0x4:
        MaskedWrite(channel->SrcAddr, val, mask & 0x07FFFFFC);
        if (channel->CR.Enable) LogPrint(LOG_SOUND|LOG_UNIMP,"Writing src while active %i\n", id);
        break;

    case 0x8:
        MaskedWrite(channel->Timer, val, mask & 0xFFFF);
        MaskedWrite(channel->LoopOffs, val>>16, (mask>>16) & 0xFFFF);
        if (channel->CR.Enable && mask & 0xFFFF) LogPrint(LOG_SOUND|LOG_UNIMP, "Writing timer while active %i\n", id);
        if (channel->CR.Enable && mask & 0xFFFF0000) LogPrint(LOG_SOUND|LOG_UNIMP, "Writing loopoffs while active %i\n", id);
        break;

    case 0xC:
        MaskedWrite(channel->SoundLen, val, mask & 0x1FFFFF);
        if (channel->CR.Enable) LogPrint(LOG_SOUND|LOG_UNIMP, "Writing len while active %i\n", id);
        break;
    }
}