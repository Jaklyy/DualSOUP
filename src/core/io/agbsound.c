#include "core/console.h"
#include "agbsound.h"
#include "dma.h"




void AGBMixer_Run(Console* sys)
{
    sys->MixerOut[0] = 0;
    sys->MixerOut[1] = 0;
    if (sys->AGBSndEn.MasterEnable)
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
    (Sched_Clock/NTR_BaseClock)*32  // 524,288 KHz
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

bool AGBSweep_Step(Console* sys, CGBSndPulse* p, u16* nfreq)
{
    u16 step = p->Latched.Sweep.Freq >> p->Regs.Sweep.Shift;
    if (p->Regs.Sweep.Dec) *nfreq = p->Latched.Sweep.Freq - step;
    else                   *nfreq = p->Latched.Sweep.Freq + step;

    if (*nfreq > 0x7FF)
    {
        CGBSnd_KillChannel(sys, 0);
        p->Latched.Sweep.On = false;
        return false;
    }
    return true;
}

bool AGBEnvelope_Step(const CGBSndEnvelope* e, CGBSndEnvelopeLatch* l, const timestamp now)
{
    if ((e->Pace != 0) && ((now & (GB_DIVAPUTICK(8)-1)) == 0)) // envelope tick
    {
        l->Tick--;
        if (l->Tick == 0)
        {
            s8 newVol = l->Vol;
            if (e->Inc) newVol++;
            else        newVol--;

            if (0 <= newVol && newVol <= 0xF)
                l->Vol = newVol;

            l->Tick = e->Pace;
            return true;
        }
    }
    return false;
}

constexpr u8 Duty[4] = {1, 2, 4, 6};
void AGBPulse_Run(Console* sys, const timestamp now, const u8 id)
{
    CGBSndPulse* p = &sys->CGBPulseChannel[id];

    if (p->Latched.DutyTick < (Duty[p->Regs.DutyPattern]))
    {
        sys->AGBSamples[id] = p->Latched.Envelope.Vol-8;
    }
    else
    {
        sys->AGBSamples[id] = -8;
    }
    p->Latched.DutyTick++;
    p->Latched.DutyTick &= 0x7;

    Sched_AddEvent(sys, now + (((1<<11) - p->Regs.Freq) * CGBSnd_ClockDiv[id]), Evt_CGBSnd1+id);
}

void AGBPulse_Tick(Console* sys, const timestamp now, const bool id)
{
    CGBSndPulse* p = &sys->CGBPulseChannel[id];

    if (((now & (GB_DIVAPUTICK(2)-1)) == 0) && p->Regs.UseLength) // len tick
    {
        p->Latched.LenTick--;
        if (p->Latched.LenTick == 0) CGBSnd_KillChannel(sys, id);
    }

    if ((id == 0) && ((now & (GB_DIVAPUTICK(4)-1)) == 0)) // sweep tick
    {
        p->Latched.Sweep.Tick--;
        p->Latched.Sweep.Tick &= 0x7;
        if (p->Latched.Sweep.Tick == 0)
        {
            if (p->Latched.Sweep.On)
            {
                u16 nfreq;
                if (AGBSweep_Step(sys, p, &nfreq) && (p->Regs.Sweep.Shift > 0))
                {
                    p->Regs.Freq = p->Latched.Sweep.Freq = nfreq;
                    AGBSweep_Step(sys, p, &nfreq);
                }
            }
            p->Latched.Sweep.Tick = p->Regs.Sweep.Pace;
        }
    }

    if (AGBEnvelope_Step(&p->Regs.Envelope, &p->Latched.Envelope, now))
    {
        if (p->Latched.DutyTick < (Duty[p->Regs.DutyPattern]))
            sys->AGBSamples[id] = p->Latched.Envelope.Vol-8;
    }
}
void AGBWave_Run(Console* sys, const timestamp now)
{
    CGBSndWave* w = &sys->CGBWaveChannel;
    const bool bank = w->Latched.BankCur;

    u8 sample;
    if (w->Regs.Vol == 0) sample = 0;
    else
    {
        sample = (sys->CGBWaveRAM[bank].Shift & 0xF);
        if (w->Regs.Vol > 0x3)
        {
            sample = (sample >> 1) + (sample >> 2);
        }
        else
        {
            sample = (sys->CGBWaveRAM[bank].Shift & 0xF) >> (w->Regs.Vol-1);
        }
    }
    sys->AGBSamples[2] = sample - 8;

    sys->CGBWaveRAM[bank].Shift = (sys->CGBWaveRAM[bank].Shift >> 4) | (sys->CGBWaveRAM[bank].Shift << 124);

    if (w->Regs.UseTwoBanks)
    {
        w->Latched.BankProg++;
        if (w->Latched.BankProg == (128/4))
        {
            w->Latched.BankProg = 0;
            w->Latched.BankCur = !w->Latched.BankProg;
        }
    }

    Sched_AddEvent(sys, now + (((1<<11) - w->Regs.Freq) * CGBSnd_ClockDiv[2]), Evt_CGBSnd3);
}

void AGBWave_Tick(Console* sys, const timestamp now)
{
    CGBSndWave* w = &sys->CGBWaveChannel;

    if (((now & (GB_DIVAPUTICK(2)-1)) == 0) && w->Regs.UseLength) // len tick
    {
        w->Latched.LenTick--;
        if (w->Latched.LenTick == 0) CGBSnd_KillChannel(sys, 2);
    }
}

timestamp AGBNoise_CalcTimer(CGBSndNoise* n)
{
    if (n->Regs.ClkDivider == 0)
        return (timestamp)1 << n->Regs.ClkShift;
    else
        return (timestamp)n->Regs.ClkDivider << (n->Regs.ClkShift + 1);
}

void AGBNoise_UpdateSample(Console* sys)
{
    if (!(sys->CGBNoiseChannel.Latched.LFSR & 1))
        sys->AGBSamples[3] = sys->CGBNoiseChannel.Latched.Envelope.Vol-8;
    else
        sys->AGBSamples[3] = -8;
}

void AGBNoise_Run(Console* sys, const timestamp now)
{
    CGBSndNoise* n = &sys->CGBNoiseChannel;

    bool shiftIn = (n->Latched.LFSR & 1);
    n->Latched.LFSR >>= 1;
    shiftIn ^= (n->Latched.LFSR & 1);
    n->Latched.LFSR |= shiftIn<<14;

    if (n->Regs.ShortLSFR)
    {
        n->Latched.LFSR &= ~0x40;
        n->Latched.LFSR |= shiftIn<<6;
    }

    AGBNoise_UpdateSample(sys);

    Sched_AddEvent(sys, now + (AGBNoise_CalcTimer(n) * CGBSnd_ClockDiv[3]), Evt_CGBSnd4);
}

void AGBNoise_Tick(Console* sys, const timestamp now)
{
    CGBSndNoise* n = &sys->CGBNoiseChannel;

    if (((now & (GB_DIVAPUTICK(2)-1)) == 0) && n->Regs.UseLength) // len tick
    {
        n->Latched.LenTick--;
        if (n->Latched.LenTick == 0) CGBSnd_KillChannel(sys, 3);
    }

    if (AGBEnvelope_Step(&n->Regs.Envelope, &n->Latched.Envelope, now))
        AGBNoise_UpdateSample(sys);
}

void AGBSnd_APUDivTick(Console* sys, const timestamp now)
{
    AGBPulse_Tick(sys, now, 0);
    AGBPulse_Tick(sys, now, 1);
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
    if (!sys->AGBSndEn.MasterEnable) return;

    AGBSndDMA_Sample(sys, id, now, false);
    AGBSndDMA_Sample(sys, id, now, true);
}

void AGBSound_IORead(Console* sys, const u32 addr, u32* rdata)
{
    switch (addr & 0xFC)
    {
    case 0x60: *rdata = sys->CGBPulseChannel[0].Regs.RawLoC1 & 0xFFC0FFFF; break;
    case 0x64: *rdata = sys->CGBPulseChannel[0].Regs.RawHi   & 0x4000; break;

    case 0x68: *rdata = sys->CGBPulseChannel[1].Regs.RawLoC2 & 0xFFC0; break;
    case 0x6C: *rdata = sys->CGBPulseChannel[1].Regs.RawHi   & 0x4000; break;

    case 0x70: *rdata = sys->CGBWaveChannel.Regs.RawLo & 0xFF00FFFF; break;
    case 0x74: *rdata = sys->CGBWaveChannel.Regs.RawHi & 0x4000; break;

    case 0x78: *rdata = sys->CGBNoiseChannel.Regs.RawLo & 0xFFC0; break;
    case 0x7C: *rdata = sys->CGBNoiseChannel.Regs.RawHi & 0x7FFF; break;

    case 0x80: *rdata = sys->AGBSndCR.Raw; break;
    case 0x84: *rdata = sys->AGBSndEn.Raw; break;
    case 0x88: *rdata = (sys->SoundBias & 0x3FE) | sys->SoundRate<<14; break;

    case 0x90 ... 0x9C: { // chan 3: wave ram
        u8 idx = (addr & 0xC) / 4;
        // nibbles are swapped within a byte??
        *rdata = 0;
        u32 nibblefuckery = sys->CGBWaveRAM[!sys->CGBWaveChannel.Regs.BankSelect].Word[idx];
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
    // chan 1: pulse wave w/ sweep
    case 0x60: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndPulse* p = &sys->CGBPulseChannel[0];
            //u8 oldpace = sys->CGBPulseChannel[0].Regs.Sweep.Pace;
            MaskedWrite(p->Regs.RawLoC1, wrdata, mask & 0xFF'FF'00'7F);
#if 0
            if ((mask & 0x00'00'00'FF) && ((oldpace == 0) || (sys->CGBSquare[0].SweepPace == 0))) // Sweep off or on
            {
                sys->CGBSquareLatch[0].Swp.Cur = sys->CGBSquare[0].SweepPace;
                sys->CGBSquareLatch[0].Swp.Freq = sys->CGBSndCR[0].Freq; // checkme?
            }
#endif
            if (mask & 0x00'FF'00'00) // write to length regs; update latched length value
                p->Latched.LenTick = (64 - p->Regs.Length);

            if ((mask & 0xFF'00'00'00) && !p->Regs.Envelope.DACEnable) // DAC disabled; kill channel
                CGBSnd_KillChannel(sys, 0);
        }
        break;
    }
    case 0x64: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndPulse* p = &sys->CGBPulseChannel[0];

            MaskedWrite(p->Regs.RawHi, wrdata, mask & 0x47FF);

            if ((wrdata & mask & 0x8000) // trigger channel
            && p->Regs.Envelope.DACEnable) // DAC is on
            {
                if (p->Latched.LenTick == 0) // if internal length tick is 0; reload value
                    p->Latched.LenTick = (64 - p->Regs.Length);

                p->Latched.Envelope.Tick = p->Regs.Envelope.Pace; // pace counter is reloaded
                p->Latched.Envelope.Vol = p->Regs.Envelope.StartVol; // volume is reloaded

                // internal sweep enable is set if any bits accross both sweep shift or sweep pace are set
                p->Latched.Sweep.On = (p->Regs.Sweep.Shift || p->Regs.Sweep.Pace);
                p->Latched.Sweep.Tick = p->Regs.Sweep.Pace; // reload sweep pace
                p->Latched.Sweep.Freq = p->Regs.Freq; // reload frequency

                // TODO: immediately do sweep overflow check(?)
                CGBSnd_StartChannel(sys, now, ((1<<11) - p->Regs.Freq), 0);
            }
        }
        break;
    }

    // chan 2: pulse wave
    case 0x68: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndPulse* p = &sys->CGBPulseChannel[1];

            MaskedWrite(p->Regs.RawLoC2, wrdata, mask & 0xFF'FF);

            if (mask & 0x00'00'00'FF) // write to length regs; update latched length value
                p->Latched.LenTick = (64 - p->Regs.Length);

            if ((mask & 0x00'00'FF'00) && !p->Regs.Envelope.DACEnable) // DAC disabled; kill channel
                CGBSnd_KillChannel(sys, 1);
        }
        break;
    }
    case 0x6C: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndPulse* p = &sys->CGBPulseChannel[1];

            MaskedWrite(p->Regs.RawHi, wrdata, mask & 0x47FF);

            if ((wrdata & mask & 0x8000) // trigger channel
            && p->Regs.Envelope.DACEnable) // DAC is on
            {
                if (p->Latched.LenTick == 0) // if internal length tick is 0; reload value
                    p->Latched.LenTick = (64 - p->Regs.Length);

                p->Latched.Envelope.Tick = p->Regs.Envelope.Pace; // pace counter is reloaded
                p->Latched.Envelope.Vol = p->Regs.Envelope.StartVol; // volume is reloaded

                CGBSnd_StartChannel(sys, now, ((1<<11) - p->Regs.Freq), 1);
            }
        }
        break;
    }

    // chan 3: wave
    case 0x70: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndWave* w = &sys->CGBWaveChannel;

            MaskedWrite(w->Regs.RawLo, wrdata, mask & 0xE0FF00E0);

            if (mask & 0x00'FF'00'00) // write to length regs; update latched length value
                w->Latched.LenTick = (256 - w->Regs.Length);

            if ((mask & 0x00'00'00'FF) && !w->Regs.DACEnable) // kill channel if dac disabled
                CGBSnd_KillChannel(sys, 2);
        }
        break;
    }
    case 0x74: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndWave* w = &sys->CGBWaveChannel;

            MaskedWrite(w->Regs.RawHi, wrdata, mask & 0x47FF);

            if ((wrdata & mask & 0x8000) // trigger; enable channel
            && w->Regs.DACEnable) // ...if DAC is enabled
            {
                w->Latched.BankCur = w->Regs.BankSelect;

                w->Latched.BankProg = 0;

                if (w->Latched.LenTick == 0) // if internal length tick is 0; reload value
                    w->Latched.LenTick = 256 - w->Regs.Length;

                CGBSnd_StartChannel(sys, now, ((1<<11) - w->Regs.Freq), 2);
            }
        }
        break;
    }

    // chan 4: noise
    case 0x78: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndNoise* n = &sys->CGBNoiseChannel;

            MaskedWrite(n->Regs.RawLo, wrdata, mask & 0xFF3F);

            if (mask & 0x00'00'00'FF) // write to length regs; update latched length value
                n->Latched.LenTick = (64 - n->Regs.Length);

            if ((mask & 0x00'00'FF'00) && n->Regs.Envelope.DACEnable) // DAC disabled; kill channel
                CGBSnd_KillChannel(sys, 3);
        }
        break;
    }
    case 0x7C: {
        if (sys->AGBSndEn.MasterEnable) {
            CGBSndNoise* n = &sys->CGBNoiseChannel;

            MaskedWrite(n->Regs.RawHi, wrdata, mask & 0x40FF);

            if ((wrdata & mask & 0x8000) // enable/restart channel
            && n->Regs.Envelope.DACEnable) // if DAC is on
            {
                if (n->Latched.LenTick == 0) // reload length if internal value is 0
                    n->Latched.LenTick = (64 - n->Regs.Length);

                n->Latched.Envelope.Tick = n->Regs.Envelope.Pace;
                n->Latched.Envelope.Vol = n->Regs.Envelope.StartVol;
                n->Latched.LFSR = 0x7FFF;

                CGBSnd_StartChannel(sys, now, AGBNoise_CalcTimer(&sys->CGBNoiseChannel), 3);
            }
        }
        break;
    }

    // snd control:
    case 0x80: {
        if (sys->AGBSndEn.MasterEnable) {
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
        bool olden = sys->AGBSndEn.MasterEnable;
        sys->AGBSndEn.Raw = wrdata & mask & 0x80;

        if (!sys->AGBSndEn.MasterEnable && olden) { // disable
            // reset all psg channel regs
            // checkme: reset all internal channel state too?
            memset(sys->CGBPulseChannel, 0, sizeof(sys->CGBPulseChannel));
            memset(&sys->CGBWaveChannel, 0, sizeof(sys->CGBWaveChannel));
            memset(&sys->CGBNoiseChannel, 0, sizeof(sys->CGBNoiseChannel));

            sys->AGBSndCR.Raw &= 0xFFFF0000; // reset low halfword // checkme: should it be 0xFFFC0000?

            // Kill all channels
            CGBSnd_KillChannel(sys, 0);
            CGBSnd_KillChannel(sys, 1);
            CGBSnd_KillChannel(sys, 2);
            CGBSnd_KillChannel(sys, 3);
        }
        break;
    }

    case 0x88: {
        MaskedWrite(sys->SoundBias, wrdata, mask & 0x3FE);

        u8 oldRate = sys->SoundRate;
        MaskedWrite(sys->SoundRate, wrdata>>14, (mask >> 14) & 0x3);

        if (oldRate != sys->SoundRate) sys->AudioFreqDirty = true;
        break;
    }

    case 0x90 ... 0x9C: { // chan 3: wave ram
        if (sys->AGBSndEn.MasterEnable) {
            u8 idx = (addr & 0xC)/ 4;
            // nibbles are swapped within a byte??
            u32 nibblefuckery = 0;
            for (int i = 0; i < 4; i++)
            {
                nibblefuckery |= (u32)ROR8((wrdata >> ((u32)8*i)) & 0xFF, 4) << ((u32)8*i);
            }
            MaskedWrite(sys->CGBWaveRAM[!sys->CGBWaveChannel.Regs.BankSelect].Word[idx], nibblefuckery, mask);
        }
        break;
    }

    case 0xA0: { // chan A: FIFO
        if (sys->AGBSndEn.MasterEnable) {
            MaskedWrite(sys->AGBSndFIFO[0].b32[sys->AGBSndFIFOFillPtr[0]/4], wrdata, mask);
            sys->AGBSndFIFOFillPtr[0] += 4;
            sys->AGBSndFIFOFillPtr[0] %= (countof(sys->AGBSndFIFO[0].b8));
        }
        break;
    }
    case 0xA4: { // chan B: FIFO
        if (sys->AGBSndEn.MasterEnable) {
            MaskedWrite(sys->AGBSndFIFO[1].b32[sys->AGBSndFIFOFillPtr[1]/4], wrdata, mask);
            sys->AGBSndFIFOFillPtr[1] += 4;
            sys->AGBSndFIFOFillPtr[1] %= (countof(sys->AGBSndFIFO[1].b8));
        }
        break;
    }
    }
}
