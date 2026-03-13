#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <stdckdint.h>
#include "../console.h"
#include "sound.h"
#include "dma.h"
#include "timer.h"



s32 AudioMixer_Pan(s32 sample, u8 pan, const bool left)
{
    pan += (pan == 127);
    if (left) pan = 128 - pan;
    return ((s64)sample * pan) >> 10;
}

void AudioMixer_Run(struct Console* sys, timestamp now)
{
    //if ((now/MixerDivide) <= sys->MixerLastRun) return;
    //sys->MixerLastRun = now/MixerDivide;

    sys->MixerOut[0] = 0;
    sys->MixerOut[1] = 0;
    if (sys->PowerCR7.AudioPower && sys->SoundCR.MasterEn)
    {
        for (int i = 0; i < 16; i++)
        {
            SoundChannel* chan = &sys->SoundChannels[i];
            s32 sample = chan->CurSample;

            sample <<= ((chan->CR.VolumeDivider == 3) ? 0 : (4-chan->CR.VolumeDivider));
            sample *= (chan->CR.VolumeMultiplier + (chan->CR.VolumeMultiplier == 127));

            if (i < 4) chan->MixedSample = sample;

            if ((i == 1) && sys->SoundCR.NoMixCh1) continue;
            if ((i == 3) && sys->SoundCR.NoMixCh3) continue;

            sys->MixerOut[0] += AudioMixer_Pan(sample, chan->CR.Panning, true);
            sys->MixerOut[1] += AudioMixer_Pan(sample, chan->CR.Panning, false);
        }
    }
}

void AudioMixer_Sample(struct Console* sys, timestamp now)
{
    AudioMixer_Run(sys, now);

    s16 sampleout[2] = {0, 0};
    if (sys->PowerCR7.AudioPower)
    {
        s32 out[2] = {0, 0};
        if (sys->SoundCR.MasterEn)
        {
            if (sys->SoundCR.LeftSrc == 0) out[0] = sys->MixerOut[0];
            else
            {
                if (sys->SoundCR.LeftSrc & 0x1) out[0] += AudioMixer_Pan(sys->SoundChannels[1].MixedSample, sys->SoundChannels[1].CR.Panning, true);
                if (sys->SoundCR.LeftSrc & 0x2) out[0] += AudioMixer_Pan(sys->SoundChannels[3].MixedSample, sys->SoundChannels[3].CR.Panning, true);
            }

            if (sys->SoundCR.RightSrc == 0) out[1] = sys->MixerOut[1];
            else
            {
                if (sys->SoundCR.RightSrc & 0x1) out[1] += AudioMixer_Pan(sys->SoundChannels[1].MixedSample, sys->SoundChannels[1].CR.Panning, false);
                if (sys->SoundCR.RightSrc & 0x2) out[1] += AudioMixer_Pan(sys->SoundChannels[3].MixedSample, sys->SoundChannels[3].CR.Panning, false);
            }
        }

        for (int i = 0; i < 2; i++)
        {
            out[i] = ((s64)out[i] * (sys->SoundCR.MasterVol + (sys->SoundCR.MasterVol == 127))) >> 15;

            if (sys->ConsoleModel < MODEL_TWL)
            {
                if (sys->Powman.PowerCR.SoundAmpEn)
                {
                    // convert to unsigned 10 bit
                    out[i] >>= 6;
                    out[i] += sys->SoundBias;
                    DS_CLAMP(out[i], <, 0)
                    DS_CLAMP(out[i], >, 0x3FF)

                    // convert back to signed 16 bit
                    sampleout[i] = (s16)((((float)out[i] * 0xFFFF) / 0x3FF) - 0x8000);
                }
            }
            else
            {
                DS_CLAMP(out[i], <, -0x8000)
                DS_CLAMP(out[i], >,  0x7FFF)
                sampleout[i] = out[i];
            }
        }
    }

    if (!SDL_PutAudioStreamData(sys->Aud, sampleout, sizeof(sampleout))) printf("wat %s\n", SDL_GetError());
    fwrite(sampleout, sizeof(sampleout), 1, sys->log);

    fflush(sys->log);

    sys->AudioFrac = (u64)(NTRBus_Clock + sys->AudioFrac) % SoundMixerOutput;
    Schedule_Event(sys, AudioMixer_Sample, Evt_MixAudio, now + ((u64)(NTRBus_Clock + sys->AudioFrac) / SoundMixerOutput));
}

void SoundFIFO_Fill(struct Console* sys, const u32 val, const u8 id, const timestamp now)
{
    //Scheduler_RunEventManual(sys, now, Evt_Timer7, false, false);
    SoundChannel* channel = &sys->SoundChannels[id];
    MemoryWrite(32, channel->FIFO, channel->FIFO_FillPtr, sizeof(channel->FIFO), val, 0xFFFFFFFF);

    channel->FIFO_FillPtr+=4;
    channel->FIFO_FillPtr &= 0x1F;
    channel->FIFO_Bytes+=4;
}

u32 SoundFIFO_Drain(struct Console* sys, SoundChannel* channel, u8 numbytes, const u8 id, const timestamp now)
{
    if (channel->FIFO_Bytes < numbytes) // fifo empty
    {
        LogPrint(LOG_SOUND, "SOUND FIFO OVERFLOW: Channel: %i cr:%08X p:%X m:%lX dma:%08X tim:%06X\n",
            id, channel->CR.Raw, channel->Prog, channel->SampleMax, sys->DMA7.Channels[id+DMA7_SoundBase].CR.Raw, sys->Timers7[id+4].Regs);
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
    channel->FIFO_DrainPtr+=numbytes;
    channel->FIFO_DrainPtr &= 0x1F;
    channel->FIFO_Bytes-=numbytes;
    if (channel->FIFO_Bytes <= 16)
    {
        StartSoundDMA(sys, id, now+1, false);
    }
    return ret;
}

extern void Timer7_UpdateCRs(struct Console* sys, timestamp now);
extern void SoundChannel_Disable(struct Console* sys, const u8 id, const timestamp now, const bool bushogged);
void SoundFIFO_Sample(struct Console* sys, const u8 id, const timestamp now)
{
    SoundChannel* channel = &sys->SoundChannels[id];

    if (!channel->CR.Enable)
    {
        if (!(channel->CR.Hold && (channel->CR.RepeatMode == 2 /* checkme? */)))
            channel->CurSample = 0;
        //else
        //    printf("trying to hold sample\n");

        // disable timer
        if (((id != 1) || !sys->SoundCaptures[0].CR.Enable) && ((id != 3) || !sys->SoundCaptures[1].CR.Enable))
        {
            Scheduler_RunEventManual(sys, now, Evt_Timer7, false, false);
            sys->Timers7[id+4].NeedsUpdate = true;
            sys->Timers7[id+4].BufferedRegs = 0x00'0000;
            Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
            return;
        }
    }

    if (channel->CR.Enable && ((now / MixerDivide) > channel->LastSubmit))
    {
        if (!sys->SoundCR.MasterEn) channel->LastSubmit = now / MixerDivide; // checkme?
        if ((sys->Sched.EventTimes[Evt_MixAudio]/MixerDivide) == (now/MixerDivide))
            AudioMixer_Run(sys, now);

        switch(channel->CR.Format)
        {
        case AudioFormat_PCM8:
        {
            if (channel->Prog >= PCM_Delay) // fetch pcm
            {
                if (channel->FIFO_Bytes < 1) break;
                channel->CurSample = (s16)(SoundFIFO_Drain(sys, channel, 1, id, now) << 8);
            }
            else if (channel->Prog > 0) channel->CurSample = 0;
            break;
        }
        case AudioFormat_PCM16:
        {
            if (channel->Prog >= PCM_Delay) // fetch pcm
            {
                if (channel->FIFO_Bytes < 2) break;
                channel->CurSample = (s16)SoundFIFO_Drain(sys, channel, 2, id, now);
            }
            else if (channel->Prog > 0) channel->CurSample = 0;
            break;
        }

        case AudioFormat_ADPCM:
        {
            if (channel->Prog >= ADPCM_Delay) // pcm
            {
                if (channel->Prog == channel->ADPCM_LoopStart)
                {
                    channel->ADPCM_LoopIndex = channel->ADPCM_Index;
                    channel->ADPCM_LoopSample = channel->ADPCM_Sample;
                }
                if (channel->Prog >= channel->ADPCM_LoopEnd)
                {
                    channel->ADPCM_Index = channel->ADPCM_LoopIndex;
                    channel->ADPCM_Sample = channel->ADPCM_LoopSample;
                    channel->Prog = channel->ADPCM_LoopStart;
                }

                if ((channel->Prog & 1) == (ADPCM_Delay & 1))
                {
                    if (channel->FIFO_Bytes < 1) break;
                    channel->ADPCM_Data = SoundFIFO_Drain(sys, channel, 1, id, now);
                }
                else
                {
                    channel->ADPCM_Data >>= 4;
                }

                u32 diff = ADPCM_Table[channel->ADPCM_Index] / 8;
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
            else if (channel->Prog == ADPCM_HeaderDelay) // fetch header
            {
                if (channel->FIFO_Bytes < 4) break;
                u32 header = SoundFIFO_Drain(sys, channel, 4, id, now);
                channel->ADPCM_Sample = (s16)(header & 0xFFFF);
                DS_CLAMP(channel->ADPCM_Sample, >, 0x7FFF)
                DS_CLAMP(channel->ADPCM_Sample, <, -0x7FFF)
                channel->ADPCM_LoopSample = channel->ADPCM_Sample;

                channel->ADPCM_Index = (header >> 16) & 0x7F;
                DS_CLAMP(channel->ADPCM_Index, >, 88)
                channel->ADPCM_LoopIndex = channel->ADPCM_Index;
                channel->CurSample = 0;
            } 
            else if (channel->Prog > 0) channel->CurSample = 0;
            break;
        }

        case AudioFormat_PSGNoise:
        {
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
                channel->Prog %= 8;

                channel->CurSample = (((7-channel->Prog) < ((channel->CR.WaveDuty+1) & 0x7)) ? 0x7FFF : -0x7FFF);
            }
            else channel->CurSample = 0;
            break;
        }
        }

        u32 tmp;
        if (!ckd_add(&tmp, channel->Prog, 1)) channel->Prog = tmp;

        channel->LastSubmit = now / MixerDivide;

        if (channel->Prog >= channel->SampleMax)
            SoundChannel_Disable(sys, id, now, false);
    }

    // sound capture
    if ((id == 1) || (id == 3))
    {
        SoundCapture* cap = &sys->SoundCaptures[id/2];

        if (!cap->CR.Enable || cap->Flush) return;

        AudioMixer_Run(sys, now);

        s16 out;
        if (cap->CR.Source) // channel 0/2
        {
            out = channel[id-1].MixedSample >> 11;
            if (cap->CR.Addition)
            {
                printf("addition\n");
                out += channel[id].MixedSample >> 11; // checkme: allegedly this can overflow
            }
            else if ((out < 0) && (channel[id].MixedSample < 0))
            {
                printf("capture bug\n");
                out = -0x8000; // checkme: this is a thing apparently?
            }
        }
        else // mixer
        {
            out = sys->MixerOut[id/2] >> 8;
            DS_CLAMP(out, <, -0x8000)
            DS_CLAMP(out, >,  0x7FFF)
        }


        if (cap->CR.Format) // pcm8
        {
            cap->FIFO.PCM8[cap->Prog % 4] = (out >> 8) & 0xFF;
            cap->Prog++;
            cap->Flush = ((cap->Prog % 4) == 0);
        }
        else // pcm16
        {
            cap->FIFO.PCM16[cap->Prog % 2] = out;
            cap->Prog++;
            cap->Flush = ((cap->Prog % 2) == 0);
        }

        if (cap->Flush)
        {
            sys->DMA7.ChannelTimestamps[(id/2) + DMA7_SoundCapBase] = now+1;
            DMA_Schedule(sys, false);
        }

        if (cap->Prog >= (cap->Length * (cap->CR.Format ? 4 : 2)))
        {
            if (cap->CR.NoLoop)
            {
                cap->CR.Enable = false;
                cap->CR.Addition = false; // checkme?
            }
            else
            {
                cap->Prog = 0;
            }
        }
    }
}


void SoundChannel_Disable(struct Console* sys, const u8 id, const timestamp now, const bool bushogged)
{
    // disable dma
    sys->DMA7.Channels[id+DMA7_SoundBase].CR.Repeat = false;
    sys->DMA7.Channels[id+DMA7_SoundBase].CR.Enable = false;

    sys->SoundChannels[id].CR.Enable = false;
}

void SoundChannel_KillAll(struct Console* sys, const timestamp now)
{
    Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
    for (int i = 0; i < 16; i++)
    {
        // disable dma
        sys->DMA7.Channels[i+DMA7_SoundBase].CR.Repeat = false;
        sys->DMA7.Channels[i+DMA7_SoundBase].CR.Enable = false;

        // disable timer
        sys->Timers7[i+4].NeedsUpdate = true;
        sys->Timers7[i+4].BufferedRegs = 0x00'0000;
    }

    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
}

void SoundChannel_Start(struct Console* sys, SoundChannel* channel, const u8 id, const timestamp now)
{
    if (channel->CR.Format != AudioFormat_PSGNoise) // checkme
    {
        // set up DMA
        if (channel->CR.RepeatMode == 0)
        {
            sys->DMA7.Channels[id+DMA7_SoundBase].Latched_NumWords = 4;
            sys->DMA7.Channels[id+DMA7_SoundBase].NumWords = 4;
            sys->DMA7.Channels[id+DMA7_SoundBase].CR.Repeat = true;
        }
        else
        {
            sys->DMA7.Channels[id+DMA7_SoundBase].Latched_NumWords = channel->SoundLen + channel->LoopOffs;
            sys->DMA7.Channels[id+DMA7_SoundBase].NumWords = channel->SoundLen;
            sys->DMA7.Channels[id+DMA7_SoundBase].CR.Repeat = (channel->CR.RepeatMode == 1);
        }
        sys->DMA7.Channels[id+DMA7_SoundBase].Latched_SrcAddr = channel->SrcAddr;
        sys->DMA7.Channels[id+DMA7_SoundBase].SrcAddr = channel->SrcAddr + ((u32)(channel->LoopOffs)*4);
        sys->DMA7.Channels[id+DMA7_SoundBase].CR.SourceCR = (channel->CR.RepeatMode == 1) ? 3 : 1;
        sys->DMA7.Channels[id+DMA7_SoundBase].CR.Enable = true;
    }

    // set up timers
    Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
    //if (sys->Timers7[id+4].CR.Enable) printf("timer fucked it all up!\n"); // todo: is this actually a problem?
    sys->Timers7[id+4].NeedsUpdate = true;
    sys->Timers7[id+4].CR.Enable = false; // hacky?
    sys->Timers7[id+4].BufferedRegs = 0xC0'0000 /* Enable, IRQ */ | channel->Timer;
    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);

    if (channel->CR.RepeatMode == 2)
    {
        switch(channel->CR.Format)
        {
        case AudioFormat_PCM8:
            channel->SampleMax = ((channel->SoundLen + channel->LoopOffs)*4) + PCM_Delay;
            break;
        case AudioFormat_PCM16:
            channel->SampleMax = ((channel->SoundLen + channel->LoopOffs)*2) + PCM_Delay;
            break;
        case AudioFormat_ADPCM:
            channel->SampleMax = ((channel->SoundLen + channel->LoopOffs-1)*8) + ADPCM_Delay;
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
            channel->ADPCM_LoopStart = ((channel->LoopOffs-1)*8) + ADPCM_Delay;
            channel->ADPCM_LoopEnd = ((channel->SoundLen + channel->LoopOffs-1)*8) + ADPCM_Delay;
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
    if ((addr & 0xF) != 0) return 0; // checkme: supposedly only each channel's control reg can be read?
    u8 id = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[id];

    return channel->CR.Raw;
}

void SoundChannel_IOWrite(struct Console* sys, const u32 addr, const u32 val, const u32 mask, const timestamp now)
{
    if (!sys->PowerCR7.AudioPower) return; // read only
    u8 id = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[id];

    switch(addr & 0xC)
    {
    case 0x0:
        Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
        u32 oldcr = channel->CR.Raw;
        MaskedWrite(channel->CR.Raw, val, mask & 0xFF7F837F);
        if (channel->CR.Enable && (oldcr>>31) && ((oldcr >> 24) ^ (channel->CR.Raw >> 24))) LogPrint(LOG_SOUND, "Updating sound CR while active %i\n", id);
        if (channel->CR.Enable ^ (oldcr>>31))
        {
            if (channel->CR.Enable)
            {
                SoundChannel_Start(sys, channel, id, now);
            }
            else
            {
                SoundChannel_Disable(sys, id, now, true);
                // disable timer
                /*Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
                sys->Timers7[id+4].NeedsUpdate = true;
                sys->Timers7[id+4].BufferedRegs = 0x00'0000;
                Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);*/
            }
        }
        if (!channel->CR.Enable && (!channel->CR.Hold && (oldcr & (1<<15)))) // clear hold (CHECKME?)
            channel->CurSample = 0;
        break;

    case 0x4:
        MaskedWrite(channel->SrcAddr, val, mask & 0x07FF'FFFC);
        if (channel->CR.Enable) LogPrint(LOG_SOUND,"Writing src while active %i\n", id);
        break;

    case 0x8:
        MaskedWrite(channel->Timer, val, mask & 0xFFFF);
        if (sys->Timers7[id+4].CR.Enable && (mask & 0xFFFF))
        {
            Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
            sys->Timers7[id+4].NeedsUpdate = true;
            sys->Timers7[id+4].BufferedRegs = 0xC0'0000 | (val & 0xFFFF);
            Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
        }

        MaskedWrite(channel->LoopOffs, val>>16, (mask>>16) & 0xFFFF);
        if (channel->CR.Enable && mask & 0xFFFF0000) LogPrint(LOG_SOUND, "Writing loopoffs while active %i\n", id);
        break;

    case 0xC:
        MaskedWrite(channel->SoundLen, val, mask & 0x003F'FFFF);
        if (channel->CR.Enable) LogPrint(LOG_SOUND, "Writing len while active %i\n", id);
        break;
    }
}

void SoundCapture_CRWrite(struct Console* sys, const u8 val, const timestamp now, const u8 id)
{
    SoundCapture* cap = &sys->SoundCaptures[id];
    bool olden = cap->CR.Enable;
    cap->CR.Raw = val & 0x8F;
    if (cap->CR.Enable)
    {
        if (!olden)
        {
            sys->DMA7.Channels[DMA7_SoundCapBase+id].Latched_NumWords = 0;
            sys->DMA7.Channels[DMA7_SoundCapBase+id].DstAddr = cap->DstAddr;
            sys->DMA7.Channels[DMA7_SoundCapBase+id].NumWords = cap->Length;
            // set up timers
            Scheduler_RunEventManual(sys, now, Evt_Timer7, false, true);
            sys->Timers7[id+4].NeedsUpdate = true;
            sys->Timers7[id+4].CR.Enable = false; // hacky?
            sys->Timers7[id+4].BufferedRegs = 0xC0'0000 /* Enable, IRQ */ | sys->SoundChannels[(id*2)+1].Timer;
            Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, now+1);
        }
    }
    //else cap->CR.Addition = false; // checkme?
}
