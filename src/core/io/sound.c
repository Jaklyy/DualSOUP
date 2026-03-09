#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <stdckdint.h>
#include "../console.h"
#include "sound.h"
#include "dma.h"
#include "timer.h"



void AudioMixer_Run(struct Console* sys, timestamp now)
{
    s16 sampleout[2] = {0, 0};

    //printf("mixing %lu\n", now);
    if (sys->SoundCR.MasterEn && sys->PowerCR7.SpeakerPower)
    {
        s32 mixerout[2] = {0, 0};


        u16 mixmaskl = 0;
        u16 mixmaskr = 0;
        if (sys->SoundCR.LeftSrc & 0x1) mixmaskl |= (1<<1);
        if (sys->SoundCR.LeftSrc & 0x2) mixmaskl |= (1<<3);
        if (sys->SoundCR.RightSrc & 0x1) mixmaskr |= (1<<1);
        if (sys->SoundCR.RightSrc & 0x2) mixmaskr |= (1<<3);
        u32 mixmask = 0xFFFF;
        if (sys->SoundCR.NoMixCh1) mixmask &= ~(1<<1);
        if (sys->SoundCR.NoMixCh3) mixmask &= ~(1<<3);
        if (!mixmaskl) mixmaskl = mixmask;
        if (!mixmaskr) mixmaskr = mixmask;

        for (int i = 0; i < 16; i++)
        {
            SoundChannel* chan = &sys->SoundChannels[i];
            s32 sample = chan->CurSample;
            //chan->CurSample = 0;
            sample <<= ((chan->CR.VolumeDivider == 3) ? 0 : (4-chan->CR.VolumeDivider));
            sample *= chan->CR.VolumeMultiplier + (chan->CR.VolumeMultiplier == 127);
            u8 panr = chan->CR.Panning + (chan->CR.Panning == 127);
            u8 panl = 128-panr;

            if (mixmaskl & (1<<i)) mixerout[0] += ((s64)sample * panl) >> 10;
            if (mixmaskr & (1<<i)) mixerout[1] += ((s64)sample * panr) >> 10;
        }

        for (int i = 0; i < 2; i++)
        {
            mixerout[i] = ((s64)mixerout[i] * (sys->SoundCR.MasterVol + (sys->SoundCR.MasterVol == 127))) >> 21;
            mixerout[i] += sys->SoundBias;
            DS_CLAMP(mixerout[i], <, 0)
            DS_CLAMP(mixerout[i], >, 0x3FF)

            // convert to signed 16 bit
            sampleout[i] = ((mixerout[i] * 0xFFFF) / 0x3FF) - 0x8000;
        }
    }
    if (!SDL_PutAudioStreamData(sys->Aud, sampleout, 4)) printf("wat %s\n", SDL_GetError());

    sys->AudioFrac = (u64)(NTRBus_Clock + sys->AudioFrac) % SoundMixerOutput;
    Schedule_Event(sys, AudioMixer_Run, Evt_MixAudio, now + ((u64)(NTRBus_Clock + sys->AudioFrac) / SoundMixerOutput));
}

void SoundFIFO_Fill(struct Console* sys, const u32 val, const u8 id, const timestamp now)
{
    //printf("try fill %i\n", id);
    //Scheduler_RunEventManual(sys, now, Evt_Timer7, false, false);
    SoundChannel* channel = &sys->SoundChannels[id];
    MemoryWrite(32, channel->FIFO, channel->FIFO_FillPtr, sizeof(channel->FIFO), val, 0xFFFFFFFF);
    //if (id == 10) printf("fill\n");
    channel->FIFO_FillPtr+=4;
    channel->FIFO_FillPtr &= 0x1F;
    channel->FIFO_Bytes+=4;
}

u32 SoundFIFO_Drain(struct Console* sys, SoundChannel* channel, u8 numbytes, const u8 id, const timestamp now)
{
    if (channel->FIFO_Bytes < numbytes) // fifo empty
    {
        LogPrint(LOG_SOUND, "SOUND FIFO OVERFLOW: Channel: %i cr:%08X p:%X m:%lX dma:%08X tim:%06X dini:%08X din2:%08X\n",
            id, channel->CR.Raw, channel->Prog, channel->SampleMax, sys->DMA7.Channels[id].CR.Raw, sys->Timers7[id+4].Regs, channel->DMAINIT, channel->DMAINIT2);
        //if (id == 10) CrashSpectacularly("woah\n");
        return 0;
    }
    u32 ret;
    switch(numbytes)
    {
        case 1: ret = MemoryRead(8, channel->FIFO, channel->FIFO_DrainPtr, sizeof(channel->FIFO)); break;
        case 2: ret = MemoryRead(16, channel->FIFO, channel->FIFO_DrainPtr, sizeof(channel->FIFO)); break;
        case 4: ret = MemoryRead(32, channel->FIFO, channel->FIFO_DrainPtr, sizeof(channel->FIFO)); break;
        default: unreachable(); // if you do this you stupid
    }
    //if (id == 10) printf("drain %i\n", numbytes);
    channel->FIFO_DrainPtr+=numbytes;
    channel->FIFO_DrainPtr &= 0x1F;
    channel->FIFO_Bytes-=numbytes;
    if (channel->FIFO_Bytes <= 16)
    {
        StartSoundDMA(sys, id, now+1, false);
    }
    return ret;
}

extern void SoundChannel_Disable(struct Console* sys, const u8 id, const timestamp now, const bool bushogged);
void SoundFIFO_Sample(struct Console* sys, const u8 id, const timestamp now)
{
    //printf("try sample %i %lu %06X\n", id, now, sys->Timers7[id+4].Regs);
    SoundChannel* channel = &sys->SoundChannels[id];

    if (channel->Prog >= channel->SampleMax)
    {
        //printf("stopping %i automatically\n", id);
        SoundChannel_Disable(sys, id, now, false);
        return;
    }

    switch(channel->CR.Format)
    {
    case AudioFormat_PCM8:
    {
        if (channel->Prog >= 3) // fetch pcm
        {
             channel->CurSample = SoundFIFO_Drain(sys, channel, 1, id, now) << 8;
        }
        else channel->CurSample = 0;
        break;
    }
    case AudioFormat_PCM16:
    {
        if (channel->Prog >= 3) // fetch pcm
        {
            channel->CurSample = SoundFIFO_Drain(sys, channel, 2, id, now);
        }
        else channel->CurSample = 0;
        break;
    }

    case AudioFormat_ADPCM:
    {
        if (channel->Prog >= 12) // pcm
        {
            if (!(channel->Prog & 1))
            {
                channel->ADPCM_Data = SoundFIFO_Drain(sys, channel, 1, id, now);
                //if (channel->Prog-13 == channel->ADPCM_LoopStart) printf("loop data: %02X\n", channel->ADPCM_Data);
            }
            else
            {
                channel->ADPCM_Data >>= 4;
            }

            u16 diff = ADPCM_Table[channel->ADPCM_Index] / 8;
            if (channel->ADPCM_Data & 0x1) diff += ADPCM_Table[channel->ADPCM_Index] / 4;
            if (channel->ADPCM_Data & 0x2) diff += ADPCM_Table[channel->ADPCM_Index] / 2;
            if (channel->ADPCM_Data & 0x4) diff += ADPCM_Table[channel->ADPCM_Index];

            if (channel->ADPCM_Data & 0x8)
            {
                channel->ADPCM_Sample -= diff;
                DS_CLAMP(channel->ADPCM_Sample, <, -0x7FFF)
            }
            else
            {
                channel->ADPCM_Sample += diff;
                DS_CLAMP(channel->ADPCM_Sample, >, 0x7FFF)
            }
            channel->CurSample = channel->ADPCM_Sample;

            channel->ADPCM_Index += ADPCM_IndexTable[channel->ADPCM_Data & 0x7];
            DS_CLAMP(channel->ADPCM_Index, >, 88)
            DS_CLAMP(channel->ADPCM_Index, <, 0)
        }
        else if (channel->Prog == 8) // fetch header
        {
            u32 header = SoundFIFO_Drain(sys, channel, 4, id, now);
            channel->ADPCM_Sample = (s16)header;// & 0xFFFF;
            DS_CLAMP(channel->ADPCM_Sample, >, 0x7FFF)
            DS_CLAMP(channel->ADPCM_Sample, <, -0x7FFF)
            channel->ADPCM_LoopSample = channel->ADPCM_Sample;

            channel->ADPCM_Index = (header >> 16) & 0x7F;
            DS_CLAMP(channel->ADPCM_Index, >, 88)
            channel->ADPCM_LoopIndex = channel->ADPCM_Index;
            channel->CurSample = 0;
        }
        else channel->CurSample = 0;

        u32 tmp;
        if (!ckd_add(&tmp, channel->Prog, 1)) channel->Prog = tmp;

        if (channel->Prog-12 == channel->ADPCM_LoopStart)
        {
            //channel->CurSample = 0;
            //printf("loopset: %i %i\n", id, channel->Prog);
            channel->ADPCM_LoopIndex = channel->ADPCM_Index;
            channel->ADPCM_LoopSample = channel->ADPCM_Sample;
        }
        if (channel->Prog-12 == channel->ADPCM_LoopEnd)
        {
            channel->ADPCM_Index = channel->ADPCM_LoopIndex;
            channel->ADPCM_Sample = channel->ADPCM_LoopSample;
            channel->Prog = channel->ADPCM_LoopStart+12;
            //printf("loopret: %i %i\n", id, channel->Prog);
        }

        //printf("%04hX\n", channel->CurSample);
        return;
    }

    case AudioFormat_PSGNoise:
    {
        // TODO
        if (id >= 14) // noise
        {
            if (channel->Noise_Cur & 0x1)
            {
                channel->Noise_Cur >>= 1;
                channel->Noise_Cur ^= 0x6000;

                channel->CurSample = -0x7FFF;
            }
            else
            {
                channel->Noise_Cur >>= 1;

                channel->CurSample = 0x7FFF;
            }
        }
        else if (id >= 8) // wave
        {
            u8 cur = (channel->Prog) % 8;

            channel->CurSample = (((7-cur) < ((channel->CR.WaveDuty+1) & 0x7)) ? 0x7FFF : -0x7FFF);
        }
        else channel->CurSample = 0;
        channel->Prog++; // allow this to overflow
        return;
    }
    }

    u32 tmp;
    if (!ckd_add(&tmp, channel->Prog, 1)) channel->Prog = tmp;

    //printf("%i %08X %08X %08X %08X %i %lu\n", id, channel->Prog, channel->SampleMax, sys->Timers7[id+4].Counter, sys->Timers7[id+4].Reload, sys->Timers7[id+4].Regs, now);
}


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
    //Timer7_UpdateCRs(sys, now); // hack
    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);

    if (!(sys->SoundChannels[id].CR.Hold && (sys->SoundChannels[id].CR.RepeatMode & 2))) sys->SoundChannels[id].CurSample = 0;

    //printf("channel stop %i %i\n", id, sys->SoundChannels[id].CR.RepeatMode);

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
        //printf("start channel: %i %08X %08X\n", id, channel->SrcAddr, channel->SrcAddr + (channel->LoopOffs*4));
        if (channel->CR.RepeatMode == 0)
        {
            sys->DMA7.Channels[id].Latched_NumWords = 4;
            sys->DMA7.Channels[id].NumWords = 4;
            sys->DMA7.Channels[id].CR.Repeat = true;
        }
        else
        {
            sys->DMA7.Channels[id].Latched_NumWords = channel->SoundLen + channel->LoopOffs;
            sys->DMA7.Channels[id].NumWords = channel->SoundLen;
            sys->DMA7.Channels[id].CR.Repeat = (channel->CR.RepeatMode == 1);
        }
        sys->DMA7.Channels[id].Latched_SrcAddr = channel->SrcAddr;
        sys->DMA7.Channels[id].SrcAddr = channel->SrcAddr;
        sys->DMA7.Channels[id].SrcAddrReload = channel->SrcAddr + ((u32)channel->LoopOffs*4);
        sys->DMA7.Channels[id].CR.SourceCR = (channel->CR.RepeatMode == 1) ? 3 : 1;
        sys->DMA7.Channels[id].CR.Enable = true;
        channel->DMAINIT = sys->DMA7.Channels[id].CR.Raw;
        channel->DMAINIT2 = sys->DMA7.Channels[id].Latched_NumWords;
    }

    // set up timers
    Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
    sys->Timers7[id+4].NeedsUpdate = true;
    sys->Timers7[id+4].BufferedRegs = 0xC0'0000 /* Enable, IRQ */ | channel->Timer;
    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
    //Timer7_UpdateCRs(sys, now); // hack

    if (channel->CR.RepeatMode == 2)// && (channel->CR.Format != AudioFormat_PSGNoise))
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
            channel->SampleMax = ((channel->SoundLen + channel->LoopOffs-1)*8) + 12;
            break;
        case AudioFormat_PSGNoise:
            channel->SampleMax = u64_max;
            break;
        }
    }
    else channel->SampleMax = u64_max;

    if (channel->CR.Format == AudioFormat_ADPCM)
    {
        if (channel->CR.RepeatMode == 1)
        {
            channel->ADPCM_LoopStart = ((channel->LoopOffs-1)*8);
            channel->ADPCM_LoopEnd = ((channel->SoundLen + channel->LoopOffs-1)*8);
        }
        else
        {
            channel->ADPCM_LoopStart = u64_max;
            channel->ADPCM_LoopEnd = u64_max;
        }
    }

    // reset noise prng
    channel->Noise_Cur = 0x7FFF;

    // reset fifo
    channel->FIFO_Bytes = 0;
    channel->FIFO_DrainPtr = 0;
    channel->FIFO_FillPtr = 0;
    channel->Prog = 0;
    StartSoundDMA(sys, id, now+1, true);
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

u32 SoundChannel_IORead(struct Console* sys, const u32 addr, const timestamp now)
{
    //printf("%08X\n", addr);
    if ((addr & 0xF) != 0) return 0; // checkme: supposedly only each channel's control reg can be read?
    u8 id = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[id];

    //printf("%i %i %i\n", id, channel->CR.Enable, channel->CR.RepeatMode);
    return channel->CR.Raw;
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
        if (channel->CR.Enable && (oldcr>>31) && ((oldcr >> 24) ^ (channel->CR.Raw >> 24))) LogPrint(LOG_SOUND, "Updating sound CR while active %i\n", id);
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
        MaskedWrite(channel->SrcAddr, val, mask & 0x07FF'FFFC);
        if (channel->CR.Enable) LogPrint(LOG_SOUND,"Writing src while active %i\n", id);
        break;

    case 0x8:
        MaskedWrite(channel->Timer, val, mask & 0xFFFF);
        MaskedWrite(channel->LoopOffs, val>>16, (mask>>16) & 0xFFFF);
        if (channel->CR.Enable && mask & 0xFFFF)
        {
            //Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
            //LogPrint(LOG_SOUND, "Writing timer while active %i\n", id);
            //sys->Timers7[id+4].BufferedRegs = 0xC0000 | (val & 0xFFFF);
            //Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
        }
        if (channel->CR.Enable && mask & 0xFFFF0000) LogPrint(LOG_SOUND, "Writing loopoffs while active %i\n", id);
        break;

    case 0xC:
        MaskedWrite(channel->SoundLen, val, mask & 0x003F'FFFF);
        if (channel->CR.Enable) LogPrint(LOG_SOUND, "Writing len while active %i\n", id);
        break;
    }
}