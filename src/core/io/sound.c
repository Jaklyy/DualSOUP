#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <stdckdint.h>
#include "core/console.h"
#include "sound.h"
#include "core/scheduler.h"
#include "dma.h"
#include "timer.h"



s32 AudioMixer_Pan(s32 sample, u8 pan, const bool left)
{
    pan += (pan == 127);
    if (left) pan = 128 - pan;
    return ((s64)sample * pan) >> 10;
}

void AudioMixer_Run(Console* sys, timestamp now [[maybe_unused]])
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

void AudioMixer_Sample(Console* sys, timestamp now)
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

            if (sys->SysCfg.NTRAudioOut == NTRAudioOut_10)
            {
                if (sys->PMIC.PowerCR.SoundAmpEn)
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

#ifdef DUMPAUDIO
    fwrite(sampleout, sizeof(sampleout), 1, sys->log);
    fflush(sys->log);
#endif

    Sched_AddEvent(sys, now + DSClk33(((timestamp)NTR_SysClock + sys->AudioFrac) / SoundMixerOutput), Evt_MixAudio);
    sys->AudioFrac = 0;//((timestamp)NTR_SysClock + sys->AudioFrac) % SoundMixerOutput;
}

void SoundFIFO_Fill(Console* sys, const u32 val, const u8 num)
{
    //printf("fill %i\n", num);
    SoundChannel* channel = &sys->SoundChannels[num];
    MemoryWrite(32, channel->FIFO, channel->FIFO_FillPtr, sizeof(channel->FIFO), val, 0xFFFFFFFF);

    channel->FIFO_FillPtr+=4;
    channel->FIFO_FillPtr &= 0x1F;
    channel->FIFO_Bytes+=4;
}

u32 SoundFIFO_Drain(Console* sys, SoundChannel* channel, u8 numbytes, const u8 num, const timestamp now)
{
    //printf("drain %i\n", num);
    if (channel->FIFO_Bytes < numbytes) // fifo empty
    {
        LogPrint(LOG_SOUND, "SOUND FIFO OVERFLOW: Channel: %i cr:%08X p:%X m:%lX dma:%08X tim:%06X\n",
            num, channel->CR.Raw, channel->Prog, channel->SampleMax, sys->DMA7[num+DMA7_SoundBase].CR.Raw, sys->TimersSound[num].Regs);
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
        StartSoundDMA(sys, num, now+1);
    }
    return ret;
}

void SoundChannel_Disable(Console* sys, const u8 num, timestamp now)
{
    // disable dma
    sys->DMA7[num+DMA7_SoundBase].CR.Repeat = false;
    sys->DMA7[num+DMA7_SoundBase].CR.Enable = false;
    sys->DMA7[num+DMA7_SoundBase].Latched_NumWords = 0;
    sys->DMA7[num+DMA7_SoundBase].NumWords = 0;
    sys->DMA7[num+DMA7_SoundBase].WriteCur = 0; // checkme?
    sys->DMA7[num+DMA7_SoundBase].BurstMax = 0; // checkme?
    sys->TimersSound[num].NeedsUpdate = true;
    sys->TimersSound[num].BufferedRegs &= 0xFFFF; // checkme?
    Sched_AddEvent(sys, now+DSClk33(1), Evt_TimerSnd0CR + num);

    sys->SoundChannels[num].CR.Enable = false;
}

void SoundFIFO_Sample(Console* sys, const u8 num, const timestamp now)
{
    SoundChannel* channel = &sys->SoundChannels[num];

    if (!channel->CR.Enable)
    {
        if (!(channel->CR.Hold && (channel->CR.RepeatMode == 2 /* checkme? */)))
            channel->CurSample = 0;
        //else
        //    printf("trying to hold sample\n");

        // disable timer
        if (((num != 1) || !sys->SoundCaptures[0].CR.Enable) && ((num != 3) || !sys->SoundCaptures[1].CR.Enable))
        {
            sys->TimersSound[num].NeedsUpdate = true;
            sys->TimersSound[num].BufferedRegs &= 0xFFFF;

            Sched_AddEvent(sys, now+DSClk33(1), Evt_TimerSnd0CR + num);
            return;
        }
    }

    if (channel->CR.Enable && ((now / MixerDivide) > channel->LastSubmit))
    {
        if (!sys->SoundCR.MasterEn) channel->LastSubmit = now / MixerDivide; // checkme?
        if ((Sched_GetTime(&sys->Sched, Evt_MixAudio)/MixerDivide) == (now/MixerDivide))
            AudioMixer_Run(sys, now);

        switch(channel->CR.Format)
        {
        case AudioFormat_PCM8:
        {
            if (channel->Prog >= PCM_Delay) // fetch pcm
            {
                if (channel->FIFO_Bytes < 1) break;
                channel->CurSample = (s16)(SoundFIFO_Drain(sys, channel, 1, num, now) << 8);
            }
            else if (channel->Prog > 0) channel->CurSample = 0;
            break;
        }
        case AudioFormat_PCM16:
        {
            if (channel->Prog >= PCM_Delay) // fetch pcm
            {
                if (channel->FIFO_Bytes < 2) break;
                channel->CurSample = (s16)SoundFIFO_Drain(sys, channel, 2, num, now);
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
                    channel->ADPCM_Data = SoundFIFO_Drain(sys, channel, 1, num, now);
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
                u32 header = SoundFIFO_Drain(sys, channel, 4, num, now);
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
            if (num >= 14) // noise
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
            else if (num >= 8) // wave
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
            SoundChannel_Disable(sys, num, now);
    }

    // sound capture
    if ((num == 1) || (num == 3))
    {
        SoundCapture* cap = &sys->SoundCaptures[num/2];

        if (!cap->CR.Enable || cap->Flush) return;

        AudioMixer_Run(sys, now);

        s16 out;
        if (cap->CR.Source) // channel 0/2
        {
            out = channel[num-1].MixedSample >> 11;
            if (cap->CR.Addition)
            {
                printf("addition\n");
                out += channel[num].MixedSample >> 11; // checkme: allegedly this can overflow
            }
            else if ((out < 0) && (channel[num].MixedSample < 0))
            {
                printf("capture bug\n");
                out = -0x8000; // checkme: this is a thing apparently?
            }
        }
        else // mixer
        {
            s32 tmp = sys->MixerOut[num/2] >> 8;
            DS_CLAMP(tmp, <, -0x8000)
            DS_CLAMP(tmp, >,  0x7FFF)
            out = tmp;
        }


        if (cap->CR.Format) // pcm8
        {
            cap->FIFO.PCM8[cap->Prog % sizeof(cap->FIFO)] = (out >> 8) & 0xFF;
            cap->Prog += 1;
        }
        else // pcm16
        {
            cap->FIFO.PCM16[(cap->Prog / 2) % (sizeof(cap->FIFO) / 2)] = out;
            cap->Prog += 2;
        }
        cap->Flush = ((cap->Prog % sizeof(cap->FIFO)) == 0);

        if (cap->Flush)
        {
            StartSoundCapDMA(sys, num/2, now+1);
        }

        if (cap->Prog >= (cap->LatchedLength * sizeof(u32)))
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

#if 0
void SoundChannel_KillAll(Console* sys, const timestamp now)
{
    for (int i = 0; i < 16; i++)
    {
        // disable dma
        sys->DMA7[i+DMA7_SoundBase].CR.Repeat = false;
        sys->DMA7[i+DMA7_SoundBase].CR.Enable = false;

        // disable timer
        sys->TimersSound[i].NeedsUpdate = true;
        sys->TimersSound[i].BufferedRegs = 0x00'0000;
    }

    Sched_AddEvent(sys, now+DSClk33(1), Evt_Timer7);
    sys->timertemp7 = TIMER_UPDATECR;
}
#endif

void SoundChannel_Start(Console* sys, SoundChannel* channel, const u8 num, const timestamp now)
{
    if (channel->CR.Format != AudioFormat_PSGNoise) // checkme
    {
        // set up DMA
        if (channel->CR.RepeatMode == 0)
        {
            sys->DMA7[num+DMA7_SoundBase].Latched_NumWords = 4;
            sys->DMA7[num+DMA7_SoundBase].NumWords = 4;
            sys->DMA7[num+DMA7_SoundBase].CR.Repeat = true;
        }
        else
        {
            sys->DMA7[num+DMA7_SoundBase].Latched_NumWords = channel->SoundLen + channel->LoopOffs;
            sys->DMA7[num+DMA7_SoundBase].NumWords = channel->SoundLen;
            sys->DMA7[num+DMA7_SoundBase].CR.Repeat = (channel->CR.RepeatMode == 1);
        }
        sys->DMA7[num+DMA7_SoundBase].Latched_SrcAddr = channel->SrcAddr;
        sys->DMA7[num+DMA7_SoundBase].SrcAddr = channel->SrcAddr + ((u32)(channel->LoopOffs)*4);
        sys->DMA7[num+DMA7_SoundBase].CR.SourceCR = (channel->CR.RepeatMode == 1) ? 3 : 1;
        sys->DMA7[num+DMA7_SoundBase].CR.Enable = true;
    }

    // set up timers
    //if (sys->TimersSound[num].CR.Enable) printf("timer fucked it all up!\n"); // todo: is this actually a problem?
    sys->TimersSound[num].NeedsUpdate = true;
    sys->TimersSound[num].CR.Enable = false; // hacky?
    sys->TimersSound[num].BufferedRegs = 0x80'0000 | channel->Timer;
    Sched_AddEvent(sys, now+DSClk33(1), Evt_TimerSnd0CR + num);

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
    StartSoundDMA(sys, num, now+1);
}

void SoundChannel_TryStartAll(Console* sys, const timestamp now)
{
    for (int i = 0; i < 16; i++)
    {
        if (sys->SoundChannels[i].CR.Enable)
        {
            SoundChannel_Start(sys, &sys->SoundChannels[i], i, now);
        }
    }
}

u32 SoundChannel_IORead(Console* sys, const u32 addr)
{
    if ((addr & 0xC) != 0) return 0; // checkme: supposedly only each channel's control reg can be read?
    u8 num = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[num];

    return channel->CR.Raw;
}

void SoundChannel_IOWrite(Console* sys, const u32 addr, const u32 val, const u32 mask, const timestamp now)
{
    if (!sys->PowerCR7.AudioPower) return; // read only
    u8 num = ((addr >> 4) & 0xF);
    SoundChannel* channel = &sys->SoundChannels[num];

    switch(addr & 0xC)
    {
    case 0x0:
        u32 oldcr = channel->CR.Raw;
        MaskedWrite(channel->CR.Raw, val, mask & 0xFF7F837F);
        if (channel->CR.Enable && (oldcr>>31) && ((oldcr >> 24) ^ (channel->CR.Raw >> 24))) LogPrint(LOG_SOUND, "Updating sound CR while active %"PRIu8"\n", num);
        if (channel->CR.Enable ^ (oldcr>>31))
        {
            if (channel->CR.Enable)
            {
                SoundChannel_Start(sys, channel, num, now);
            }
            else
            {
                SoundChannel_Disable(sys, num, now);
            }
        }
        if (!channel->CR.Enable && (!channel->CR.Hold && (oldcr & (1<<15)))) // clear hold (CHECKME?)
        {
            LogPrint(LOG_SOUND,"Tentative: Hold clear? %"PRIu8"\n", num);
            channel->CurSample = 0;
        }
        break;

    case 0x4:
        MaskedWrite(channel->SrcAddr, val, mask & 0x07FF'FFFC);
        if (channel->CR.Enable) LogPrint(LOG_SOUND,"Writing src while active %"PRIu8"\n", num);
        break;

    case 0x8:
        MaskedWrite(channel->Timer, val, mask & 0xFFFF);
        if (mask & 0x0000FFFF)
        {
            sys->TimersSound[num].NeedsUpdate = true;
            sys->TimersSound[num].BufferedRegs &= 0xFF'0000;
            sys->TimersSound[num].BufferedRegs |= channel->Timer;
            Sched_AddEvent(sys, now+DSClk33(1), Evt_TimerSnd0CR + num);
        }

        MaskedWrite(channel->LoopOffs, val>>16, (mask>>16) & 0xFFFF);
        if (channel->CR.Enable && mask & 0xFFFF0000) LogPrint(LOG_SOUND, "Writing loopoffs while active %"PRIu8"\n", num);
        break;

    case 0xC:
        MaskedWrite(channel->SoundLen, val, mask & 0x003F'FFFF);
        if (channel->CR.Enable) LogPrint(LOG_SOUND, "Writing len while active %"PRIu8"\n", num);
        break;
    }
}

void SoundCapture_CRWrite(Console* sys, const u8 val, const timestamp now, const u8 num)
{
    SoundCapture* cap = &sys->SoundCaptures[num];
    bool olden = cap->CR.Enable;
    cap->CR.Raw = val & 0x8F;
    if (cap->CR.Enable ^ olden)
    {
        if (cap->CR.Enable)
        {
            cap->Prog = 0;
            cap->Flush = false;
            cap->LatchedLength = (cap->Length + (cap->Length == 0)) * sizeof(u32);
            sys->DMA7[DMA7_SoundCapBase+num].Latched_NumWords = 0;
            sys->DMA7[DMA7_SoundCapBase+num].DstAddr = cap->DstAddr;
            // set up timers
            if (!sys->SoundChannels[(num*2)+1].CR.Enable)
            {
                sys->TimersSound[(num*2)+1].NeedsUpdate = true;
                sys->TimersSound[(num*2)+1].CR.Enable = false; // hacky?
                sys->TimersSound[(num*2)+1].BufferedRegs = 0xC0'0000 /* Enable, IRQ */ | sys->SoundChannels[(num*2)+1].Timer;
                Sched_AddEvent(sys, now+DSClk33(1), Evt_TimerSnd0CR+((num*2)+1));
            }
        }
    }
    //else cap->CR.Addition = false; // checkme?
}
