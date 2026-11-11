#include "core/console.h"
#include "agbsound.h"
#include "dma.h"




void AGBMixer_Run(Console* sys)
{
    sys->MixerOut[0] = 0;
    sys->MixerOut[1] = 0;
    if (sys->AGBSndEn.MEnable)
    {
        const u8 CGBPan[2] = {sys->AGBSndCR.LeftVolume, sys->AGBSndCR.RightVolume};
        const bool En1[2] = {sys->AGBSndCR.Chan1LEn, sys->AGBSndCR.Chan1REn};
        const bool En2[2] = {sys->AGBSndCR.Chan2LEn, sys->AGBSndCR.Chan2REn};
        const bool En3[2] = {sys->AGBSndCR.Chan3LEn, sys->AGBSndCR.Chan3REn};
        const bool En4[2] = {sys->AGBSndCR.Chan4LEn, sys->AGBSndCR.Chan4REn};
        const bool EnA[2] = {sys->AGBSndCR.ALEn, sys->AGBSndCR.AREn};
        const bool EnB[2] = {sys->AGBSndCR.BLEn, sys->AGBSndCR.BREn};
        for (int i = 0; i < 2; i++)
        {
            if (sys->AGBSndCR.CGBVol != 3)
            {
                if (En1[i]) sys->MixerOut[i] += sys->AGBSamples[0];
                if (En2[i]) sys->MixerOut[i] += sys->AGBSamples[1];
                if (En3[i]) sys->MixerOut[i] += sys->AGBSamples[2];
                if (En4[i]) sys->MixerOut[i] += sys->AGBSamples[3];

                sys->MixerOut[i] <<= sys->AGBSndCR.CGBVol + 5;
                sys->MixerOut[i] *= (CGBPan[i] + 1);
            }
            if (EnA[i]) sys->MixerOut[i] += sys->AGBSamples[4] << (sys->AGBSndCR.AVol+7);
            if (EnB[i]) sys->MixerOut[i] += sys->AGBSamples[5] << (sys->AGBSndCR.BVol+7);
        }
    }
}

constexpr timestamp CGBSnd_ClockDiv[4] = {
    (Sched_Clock/NTR_BaseClock)*16, // 1.048.576 MHz
    (Sched_Clock/NTR_BaseClock)*16, // 1.048.576 MHz
    (Sched_Clock/NTR_BaseClock)*8,  // 2.097.152 MHz
    (Sched_Clock/NTR_BaseClock)*32  // 262,144 KHz
};

void CGBSnd_StartChannel(Console* sys, const timestamp now, const timestamp counter, const u8 id)
{
    sys->AGBSndEn.Raw |= 1<<id;
    Sched_AddEvent(sys, (now & ~(CGBSnd_ClockDiv[id]-1)) + (counter*CGBSnd_ClockDiv[id]), Evt_CGBSnd1+id);
}

void CGBSnd_KillChannel(Console* sys, const u8 id)
{
    sys->AGBSndEn.Raw &= ~(1<<id);
    sys->AGBSamples[id] = -8;
    Sched_RemoveEvent(&sys->Sched, Evt_CGBSnd1+id);
}

void AGBSquare_Run(Console* sys, const timestamp now, const u8 id)
{
    const u8 Duty[4] = {1, 2, 4, 6};
    if (sys->CGBSquareLatch[id].Tick < (Duty[sys->CGBSquare[id].WaveDuty]))
    {
        sys->AGBSamples[id] = sys->CGBSquareLatch[id].Env.Vol-8;
    }
    else
    {
        sys->AGBSamples[id] = -8;
    }
    sys->CGBSquareLatch[id].Tick++;
    sys->CGBSquareLatch[id].Tick &= 0x7;

    Sched_AddEvent(sys, now + (((1<<11)-sys->CGBSndCR[id].Freq)*CGBSnd_ClockDiv[id]), Evt_CGBSnd1+id);
}

void AGBSquare_Tick(Console* sys, const timestamp now, const bool id)
{
    CGBSndSquare* sqr = &sys->CGBSquare[id];
    CGBSndCR* cr = &sys->CGBSndCR[id];
    CGBSquareLatch* lat = &sys->CGBSquareLatch[id];

    if (((now & (GB_DIVAPUTICK(2)-1)) == 0) && cr->UseLen) // len tick
    {
        lat->Len--;
        //lat->Len &= 0x3F;
        if (lat->Len == 0)
        {
            CGBSnd_KillChannel(sys, id);
        }
    }

    if ((id == 0) && ((now & (GB_DIVAPUTICK(4)-1)) == 0)) // sweep tick
    {
        lat->Swp.Cur--;
        lat->Swp.Cur &= 0x7;
        if (lat->Swp.Cur == 0)
        {
            if (lat->Swp.On)
            {
                u8 step = lat->Swp.Freq >> sqr->SweepShift;
                u16 nfreq;
                if (sqr->SweepDir) nfreq = lat->Swp.Freq - step;
                else               nfreq = lat->Swp.Freq + step;

                if (nfreq > 0x7FF)
                {
                    CGBSnd_KillChannel(sys, 0);
                }
                else if (sqr->SweepShift > 0) // checkme??
                {
                    cr->Freq = lat->Swp.Freq = nfreq;

                    u8 step = lat->Swp.Freq >> sqr->SweepShift;
                    if (sqr->SweepDir) nfreq = lat->Swp.Freq - step;
                    else               nfreq = lat->Swp.Freq + step;

                    if (nfreq > 0x7FF)
                    {
                        CGBSnd_KillChannel(sys, 0);
                    }
                }
            }
            lat->Swp.Cur = lat->Swp.Pace;
            lat->Swp.Pace = sqr->SweepPace;
        }
    }

    if ((sqr->EnvelopePace != 0) && ((now & (GB_DIVAPUTICK(8)-1)) == 0)) // envelope tick
    {
        lat->Env.Cur--;
        if (lat->Env.Cur == 0)
        {
            s8 newVol = lat->Env.Vol;
            if (lat->Env.Dir) newVol++;
            else              newVol--;

            if (0 <= newVol && newVol <= 0xF)
                lat->Env.Vol = newVol;

            lat->Env.Cur = sqr->EnvelopePace;
        }
    }
}

void AGBWave_Run(Console* sys, const timestamp now)
{
    const bool bank = sys->CGBWaveBankCur;
    u8 sample;
    if (sys->CGBWave.Vol == 0) sample = 0;
    else
    {
        sample = (sys->CGBWaveRAM[bank].Shift & 0xF);
        if (sys->CGBWave.Vol > 0x3)
        {
            sample = (sample >> 1) + (sample >> 2);
        }
        else
        {
            sample = (sys->CGBWaveRAM[bank].Shift & 0xF) >> (sys->CGBWave.Vol-1);
        }
    }
    sys->AGBSamples[2] = sample - 8;

    sys->CGBWaveRAM[bank].Shift = (sys->CGBWaveRAM[bank].Shift >> 4) | (sys->CGBWaveRAM[bank].Shift << 124);

    if (sys->CGBWave.WaveDimension)
    {
        sys->CGBWaveBankProg++;
        if (sys->CGBWaveBankProg == (128/4))
        {
            sys->CGBWaveBankProg = 0;
            sys->CGBWaveBankCur = !sys->CGBWaveBankCur;
        }
    }

    Sched_AddEvent(sys, now + (((1<<11)-sys->CGBSndCR[2].Freq)*CGBSnd_ClockDiv[2]), Evt_CGBSnd3);
}

void AGBWave_Tick(Console* sys, const timestamp now)
{
    CGBSndCR* cr = &sys->CGBSndCR[2];

    if (((now & (GB_DIVAPUTICK(2)-1)) == 0) && cr->UseLen) // len tick
    {
        sys->CGBWaveLen--;
        //sys->CGBWaveLen &= 0xFF;
        if (sys->CGBWaveLen == 0)
        {
            CGBSnd_KillChannel(sys, 2);
        }
    }
}

timestamp AGBNoise_CalcTimer(u8 divider, u8 shift)
{
    if (divider == 0) return (timestamp)1<<shift;
    else return (timestamp)divider<<(shift+1);
}

void AGBNoise_UpdateSample(Console* sys)
{
    if (!(sys->NoiseLFSR & 1))
        sys->AGBSamples[3] = sys->CGBNoiseEnvelope.Vol-8;
    else
        sys->AGBSamples[3] = -8;
}

void AGBNoise_Run(Console* sys, const timestamp now)
{
    bool shiftIn = (sys->NoiseLFSR & 1);
    sys->NoiseLFSR >>= 1;
    shiftIn ^= (sys->NoiseLFSR & 1);
    sys->NoiseLFSR |= shiftIn<<14;

    if (sys->CGBNoiseCR.ShortLSFR)
    {
        sys->NoiseLFSR &= ~0x40;
        sys->NoiseLFSR |= shiftIn<<6;
    }

    AGBNoise_UpdateSample(sys);

    Sched_AddEvent(sys, now + (AGBNoise_CalcTimer(sys->CGBNoiseCR.ClkDivider,sys->CGBNoiseCR.ClkShift)*CGBSnd_ClockDiv[3]), Evt_CGBSnd4);
}

void AGBNoise_Tick(Console* sys, const timestamp now)
{
    CGBSndNoiseCR* cr = &sys->CGBNoiseCR;
    CGBSndNoise* noise = &sys->CGBNoise;
    CGBEnvelope* env = &sys->CGBNoiseEnvelope;

    if (((now & (GB_DIVAPUTICK(2)-1)) == 0) && cr->UseLen) // len tick
    {
        sys->CGBNoiseLen--;
        //sys->CGBNoiseLen &= 0x3F;
        if (sys->CGBNoiseLen == 0)
            CGBSnd_KillChannel(sys, 3);
    }

    if ((noise->EnvelopePace != 0) && ((now & (GB_DIVAPUTICK(8)-1)) == 0)) // envelope tick
    {
        env->Cur--;
        if (env->Cur == 0)
        {
            s8 newVol = env->Vol;
            if (env->Dir) newVol++;
            else          newVol--;

            if (0 <= newVol && newVol <= 0xF)
                env->Vol = newVol;

            env->Cur = noise->EnvelopePace;
        }
        AGBNoise_UpdateSample(sys);
    }
}

void AGBSnd_APUDivTick(Console* sys, const timestamp now)
{
    AGBSquare_Tick(sys, now, 0);
    AGBSquare_Tick(sys, now, 1);
    AGBWave_Tick(sys, now);
    AGBNoise_Tick(sys, now);

    Sched_AddEvent(sys, now+GB_DIVAPUTICK(1), Evt_CGBSndAPUDIV);
}

void AGBSndDMA_Sample(Console* sys, const u8 id, const timestamp now, const bool b)
{
    if (id == (b ? sys->AGBSndCR.BTimerSel : sys->AGBSndCR.ATimerSel))
    {
        if (((sys->AGBSndFIFOFillPtr[b] - sys->AGBSndFIFODrainPtr[b]) % countof(sys->AGBSndFIFO[b].b8)) > 0) // yes, that is correct
        {
            sys->AGBSamples[4+b] = sys->AGBSndFIFO[b].b8[sys->AGBSndFIFODrainPtr[b]];
            sys->AGBSndFIFODrainPtr[b] += 1;
            sys->AGBSndFIFODrainPtr[b] %= countof(sys->AGBSndFIFO[b].b8);
        }

        if (((sys->AGBSndFIFOFillPtr[b] - sys->AGBSndFIFODrainPtr[b]) % countof(sys->AGBSndFIFO[b].b8)) <= (4*3))
            StartDMA(sys, now, DMAStart_AGBAudio, false);
    }
}

void AGBSoundDMATimer_Overflow(Console* sys, const u8 id, const timestamp now)
{
    if (!sys->AGBSndEn.MEnable) return;

    AGBSndDMA_Sample(sys, id, now, false);
    AGBSndDMA_Sample(sys, id, now, true);
}

void AGBSound_IORead(Console* sys, const u32 addr, u32* rdata)
{
    switch (addr & 0xFC)
    {
    case 0x60: *rdata = sys->CGBSquare[0].Raw & 0xFFC0FFFF; break;
    case 0x64: *rdata = sys->CGBSndCR[0].Raw & 0x4000; break;

    case 0x68: *rdata = (sys->CGBSquare[1].Raw >> 16) & 0xFFC0; break;
    case 0x6C: *rdata = sys->CGBSndCR[1].Raw & 0x4000; break;

    case 0x70: *rdata = sys->CGBWave.Raw & 0xFF00FFFF; break;
    case 0x74: *rdata = sys->CGBSndCR[2].Raw & 0x4000; break;

    case 0x78: *rdata = sys->CGBNoise.Raw & 0xFFC0; break;
    case 0x7C: *rdata = sys->CGBNoiseCR.Raw & 0x7FFF; break;

    case 0x80: *rdata = sys->AGBSndCR.Raw; break;
    case 0x84: *rdata = sys->AGBSndEn.Raw; break;
    case 0x88: *rdata = (sys->SoundBias & 0x3FE) | sys->SoundRate<<14; break;

    case 0x90 ... 0x9C: { // chan 3: wave ram
        u8 idx = (addr & 0xC) / 4;
        // nibbles are swapped within a byte??
        *rdata = 0;
        u32 nibblefuckery = sys->CGBWaveRAM[!sys->CGBWave.WaveBank].Word[idx];
        for (int i = 0; i < 4; i++)
        {
            *rdata |= (u32)ROR8((nibblefuckery >> ((u32)8*i)) & 0xFF, 4) << ((u32)8*i);
        }
        break;
    }
    }
}

void AGBSound_IOWrite(Console* sys, const u32 addr, u32 wrdata, const u32 mask, const timestamp now)
{
    switch (addr & 0xFC)
    {
    // chan 1: square wave w/ sweep
    case 0x60: {
        if (sys->AGBSndEn.MEnable) {
            u8 oldpace = sys->CGBSquare[0].SweepPace;
            MaskedWrite(sys->CGBSquare[0].Raw, wrdata, mask & 0xFF'FF'00'7F);

            if ((mask & 0x00'00'00'FF) && ((oldpace == 0) || (sys->CGBSquare[0].SweepPace == 0))) // Sweep off or on
            {
                sys->CGBSquareLatch[0].Swp.Pace = sys->CGBSquare[0].SweepPace;
                sys->CGBSquareLatch[0].Swp.Cur = sys->CGBSquare[0].SweepPace;
                sys->CGBSquareLatch[0].Swp.Freq = sys->CGBSndCR[0].Freq; // checkme?
            }

            if (mask & 0x00'FF'00'00) sys->CGBSquareLatch[0].Len = (64-sys->CGBSquare[0].Len);
            if ((mask & 0xFF'00'00'00) && (((sys->CGBSquare[0].Raw >> 27) & 0x1F) == 0)) // DAC off
            {
                CGBSnd_KillChannel(sys, 0);
            }
        }
        break;
    }
    case 0x64: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBSndCR[0].Raw, wrdata, mask & 0x47FF);

            if ((wrdata & mask & 0x8000) // enable channel
            && (((sys->CGBSquare[0].Raw >> 27) & 0x1F) != 0)) // DAC is on
            {
                if (sys->CGBSquare[0].Len == 0) sys->CGBSquareLatch[0].Len = (64-sys->CGBSquare[0].Len);
                sys->CGBSquareLatch[0].Env.Cur = sys->CGBSquare[0].EnvelopePace;
                sys->CGBSquareLatch[0].Env.Vol = sys->CGBSquare[0].EnvelopeVol;

                sys->CGBSquareLatch[0].Swp.On = (sys->CGBSquare[0].SweepShift || sys->CGBSquare[0].SweepPace);
                sys->CGBSquareLatch[0].Swp.Pace = sys->CGBSquare[0].SweepPace;
                sys->CGBSquareLatch[0].Swp.Cur = sys->CGBSquare[0].SweepPace;
                sys->CGBSquareLatch[0].Swp.Freq = sys->CGBSndCR[0].Freq;
                if ((sys->CGBSquare[0].SweepShift == 0)
                    || sys->CGBSquare[0].SweepDir // decrementing
                    || ((sys->CGBSquareLatch[0].Swp.Freq + (sys->CGBSquareLatch[0].Swp.Freq >> sys->CGBSquare[0].SweepShift)) <= 0x7FF))
                {
                    CGBSnd_StartChannel(sys, now, (1<<11)-sys->CGBSndCR[0].Freq, 0);
                }
                else
                {
                    CGBSnd_KillChannel(sys, 0);
                }
            }
        }
        break;
    }

    // chan 2: square wave
    case 0x68: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBSquare[1].Raw, wrdata << 16, (mask & 0xFFFF) << 16);

            if (mask & 0x00'00'00'FF) sys->CGBSquareLatch[1].Len = (64-sys->CGBSquare[1].Len);

            if ((mask & 0x00'00'FF'00) && (((sys->CGBSquare[1].Raw >> 27) & 0x1F) == 0)) // DAC off
                CGBSnd_KillChannel(sys, 1);
        }
        break;
    }
    case 0x6C: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBSndCR[1].Raw, wrdata, mask & 0x47FF);

            if ((wrdata & mask & 0x8000) // enable channel
            && (((sys->CGBSquare[1].Raw >> 27) & 0x1F) != 0)) // DAC is on
            {
                if (sys->CGBSquare[1].Len == 0) sys->CGBSquareLatch[1].Len = (64-sys->CGBSquare[1].Len);
                sys->CGBSquareLatch[1].Env.Cur = sys->CGBSquare[1].EnvelopePace;
                sys->CGBSquareLatch[1].Env.Vol = sys->CGBSquare[1].EnvelopeVol;

                CGBSnd_StartChannel(sys, now, (1<<11)-sys->CGBSndCR[1].Freq, 1);
            }
        }
        break;
    }

    // chan 3: wave
    case 0x70: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBWave.Raw, wrdata, mask & 0xE0FF00E0);
            if (mask & 0x00'FF'00'00) sys->CGBWaveLen = (256-sys->CGBWave.Len);
            if ((mask & 0x00'00'00'FF) && !sys->CGBWave.DACEnable)
                CGBSnd_KillChannel(sys, 2);
        }
        break;
    }
    case 0x74: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBSndCR[2].Raw, wrdata, mask & 0x47FF);
            if (wrdata & mask & 0x8000)
            {
                sys->CGBWaveBankCur = sys->CGBWave.WaveBank;
                sys->CGBWaveBankProg = 0;
                if (sys->CGBWaveLen == 0) sys->CGBWaveLen = 256-sys->CGBWave.Len;
                CGBSnd_StartChannel(sys, now, (1<<11)-sys->CGBSndCR[2].Freq, 2);
            }
        }
        break;
    }

    // chan 4: noise
    case 0x78: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBNoise.Raw, wrdata, mask & 0xFF3F);

            if (mask & 0x00'00'00'FF) sys->CGBNoiseLen = (64-sys->CGBNoise.Len);
            if ((mask & 0x00'00'FF'00) && (((sys->CGBNoise.Raw >> 11) & 0x1F) == 0)) // DAC off
            {
                CGBSnd_KillChannel(sys, 3);
            }
        }
        break;
    }
    case 0x7C: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->CGBNoiseCR.Raw, wrdata, mask & 0x40FF);

            if ((wrdata & mask & 0x8000) // enable channel
            && (((sys->CGBNoise.Raw >> 11) & 0x1F) != 0)) // DAC is on
            {
                if (sys->CGBNoiseLen == 0) sys->CGBNoiseLen = (64-sys->CGBNoise.Len);
                sys->CGBNoiseEnvelope.Cur = sys->CGBNoise.EnvelopePace;
                sys->CGBNoiseEnvelope.Vol = sys->CGBNoise.InitialVol;
                sys->NoiseLFSR = 0x7FFF;

                CGBSnd_StartChannel(sys, now, AGBNoise_CalcTimer(sys->CGBNoiseCR.ClkDivider,sys->CGBNoiseCR.ClkShift), 3);
            }
        }
        break;
    }

    // snd control:
    case 0x80: {
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->AGBSndCR.Raw, wrdata, mask & 0x770FFF77);
            if (wrdata & mask & 0x08000000) // reset fifo A
            {
                memset(sys->AGBSndFIFO[0].b8, 0, sizeof(sys->AGBSndFIFO[0].b8));
                sys->AGBSndFIFODrainPtr[0] = 0;
                sys->AGBSndFIFOFillPtr[0] = 0;
            }
            if (wrdata & mask & 0x80000000) // reset fifo B
            {
                memset(sys->AGBSndFIFO[1].b8, 0, sizeof(sys->AGBSndFIFO[1].b8));
                sys->AGBSndFIFODrainPtr[1] = 0;
                sys->AGBSndFIFOFillPtr[1] = 0;
            }
        }
        break;
    }
    case 0x84: {
        bool olden = sys->AGBSndEn.MEnable;
        sys->AGBSndEn.Raw = wrdata & mask & 0x80;
        if (!sys->AGBSndEn.MEnable && olden) { // disable
            // reset all psg channel regs
            sys->CGBSquare[0].Raw = 0;
            sys->CGBSquare[1].Raw = 0;
            sys->CGBWave.Raw = 0;
            sys->CGBNoise.Raw = 0;

            sys->CGBSndCR[0].Raw = 0;
            sys->CGBSndCR[1].Raw = 0;
            sys->CGBSndCR[2].Raw = 0;
            sys->CGBNoiseCR.Raw = 0;

            sys->CGBSquareLatch[0].Tick = 0;
            sys->CGBSquareLatch[1].Tick = 0;

            sys->AGBSndCR.Raw &= 0xFFFF0000; // reset low halfword // checkme: should it be 0xFFFC0000?

            CGBSnd_KillChannel(sys, 0);
            CGBSnd_KillChannel(sys, 1);
            CGBSnd_KillChannel(sys, 2);
            CGBSnd_KillChannel(sys, 3);
        }
        break;
    }

    case 0x88: {
        MaskedWrite(sys->SoundBias, wrdata, mask & 0x3FE);
        MaskedWrite(sys->SoundRate, wrdata>>14, (mask >> 14) & 0x3);
        break;
    }

    case 0x90 ... 0x9C: { // chan 3: wave ram
        if (sys->AGBSndEn.MEnable) {
            u8 idx = (addr & 0xC)/ 4;
            // nibbles are swapped within a byte??
            u32 nibblefuckery = 0;
            for (int i = 0; i < 4; i++)
            {
                nibblefuckery |= (u32)ROR8((wrdata >> ((u32)8*i)) & 0xFF, 4) << ((u32)8*i);
            }
            MaskedWrite(sys->CGBWaveRAM[!sys->CGBWave.WaveBank].Word[idx], nibblefuckery, mask);
        }
        break;
    }

    case 0xA0: { // chan A: FIFO
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->AGBSndFIFO[0].b32[sys->AGBSndFIFOFillPtr[0]/4], wrdata, mask);
            sys->AGBSndFIFOFillPtr[0] += 4;
            sys->AGBSndFIFOFillPtr[0] %= (countof(sys->AGBSndFIFO[0].b8));
        }
        break;
    }
    case 0xA4: { // chan B: FIFO
        if (sys->AGBSndEn.MEnable) {
            MaskedWrite(sys->AGBSndFIFO[1].b32[sys->AGBSndFIFOFillPtr[1]/4], wrdata, mask);
            sys->AGBSndFIFOFillPtr[1] += 4;
            sys->AGBSndFIFOFillPtr[1] %= (countof(sys->AGBSndFIFO[1].b8));
        }
        break;
    }
    }
}
