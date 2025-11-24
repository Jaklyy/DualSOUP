#include "arm.h"
#include "../../bus/ahb.h"




bool ARM9_ITCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !ARM9->CP15.CR.ITCMLoadMode && !(addr >> ARM9->CP15.ITCMShift));
}

bool ARM9_ITCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !(addr >> ARM9->CP15.ITCMShift));
}

bool ARM9_DTCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return ((addr >> ARM9->CP15.DTCMShift) == ARM9->CP15.DTCMReadBase);
}

bool ARM9_DTCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return ((addr >> ARM9->CP15.DTCMShift) == ARM9->CP15.DTCMWriteBase);
}

// this function results in the timestamp being aligned with the bus clock
void ARM9_AHBAccess(struct ARM946ES* ARM9, timestamp* ts, const bool atomic, bool* seq)
{
    // stall until aligned with external bus clock
    // TODO: a lot of this logic breaks once you realize that the arm9 isn't the only component that can change the arm9 clock.
    // I dont know how to fix that........
    const unsigned round = ((ARM9->BoostedClock) ? 3 : 1); // round up
    const unsigned shift = ((ARM9->BoostedClock) ? 2 : 1); // shift to convert between clocks
    *ts = ((*ts + round) >> shift);

    // external bus accesses have a fixed latency due to buffering.
    // this can begin while other internal arm9 buses are using the external bus, thus hiding part or even all of the latency.
    const unsigned bufferlatency = ((ARM9->BoostedClock) ? 2 : 3);

    // the arm9 has multiple components capable of accessing the bus at the same time
    // and they are capable of queueing ext. bus accesses while other int. buses are using it.
    // so we should check if arm9 already had ownership of the bus from accesses that were handled earlier.
    if (*ts < ARM9->LastBusTime)
    {
        // CHECKME: does it make sense to wait until the bus was actually attained?
        // it's more correct to do so but im not sure it matters in practice?

        // NOTE: this code path should't trigger with sequential accesses
        if (*seq) LogPrint(LOG_ALWAYS, "Sequential in codepath that sequentials should'nt be...?\n");

        // CHECKME: how exactly does latency and losing bus ownership work with sharing?
        *ts += bufferlatency;

        // make sure we dont immediately lose bus ownership
        // if we dont lose ownership we don't have to reincur latency
        if (AHB9_NegOwnership(ARM9->ARM.Sys, ts, AHB9Prio_ARM9, atomic))
            return;
        else
            *seq = false;
    }
    else
    {
        // wait until we're able to take ownership of the ARM9 side bus.
        // once we have ownership we can't lose it until the access fully ends.
        // we also can't lose ownership during an atomic access.
        if (!AHB9_NegOwnership(ARM9->ARM.Sys, ts, AHB9Prio_ARM9, atomic))
            *seq = false;
    }

    // add latency. technically still applies for sequential accesses but it's weird tm
    if (!*seq) *ts += bufferlatency;
}

u32 ARM9_AHBRead(struct ARM946ES* ARM9, timestamp* ts, const u32 addr, const u32 mask, const bool atomic, bool* seq)
{
    // handle external bus logic
    ARM9_AHBAccess(ARM9, ts, false, seq);

    // ahb accesses must not carry a burst across a 1KiB boundary
    if ((addr & (KiB(1)-1)) == 0)
    {
        *seq = false;
    }

    // actually read off of the bus
    u32 ret = AHB9_Read(ARM9->ARM.Sys, ts, addr, mask, atomic, false, seq);

    // convert clock back
    // the arm9 interacts with the bus on the rising edge of the bus clock so we get the result on the first cycle of the clock.
    // so we do this weird looking thing to get the right effect.
    *ts = ((*ts - 1) << ((ARM9->BoostedClock) ? 2 : 1)) + 1;

    return ret;
}

void ARM9_AHBWrite(struct ARM946ES* ARM9, timestamp* ts, const u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq)
{
    // handle external bus logic
    ARM9_AHBAccess(ARM9, ts, false, seq);

    // ahb accesses must not carry a burst across a 1KiB boundary
    if ((addr & (KiB(1)-1)) == 0)
    {
        *seq = false;
    }

    // actually read off of the bus
    //AHB9_Write(ARM9->ARM.Sys, ts, addr, val, mask, atomic, false, seq);

    // convert clock back
    // the arm9 interacts with the bus on the rising edge of the bus clock so we get the result on the first cycle of the clock.
    // so we do this weird looking thing to get the right effect.
    *ts = ((*ts - 1) << ((ARM9->BoostedClock) ? 2 : 1)) + 1;
}


void ARM9_RunWriteBuffer(struct ARM946ES* ARM9, const timestamp until)
{
    struct ARM9_WriteBuffer* buf = &ARM9->WBuffer;

    buf->NextStep;


    switch(buf->FIFOEntry[buf->FIFODrainPtr].Flags)
    {
    case A9WB_Addr:
    {
        buf->CurAddr = buf->FIFOEntry[buf->FIFODrainPtr].Data;
        buf->FIFODrainPtr = (buf->FIFODrainPtr + 1) % 16;
        break;
    }
    default:
    {
        break;
    }
    }
}

bool ARM9_ProgressCacheStream(timestamp* ts, struct ARM9_CacheStream* stream, u32* ret, const bool seq)
{
    // check if stream is even active
    if (stream->Prog >= 7) return false;

    // cache streaming is interrupted on non-sequential accesses, or if we overshoot the window of a given fetch
    if (!seq || (*ts > stream->Times[stream->Prog])) // note: this second check isn't really useful on the data bus...?
    {
        // set stream progress to done.
        stream->Prog = 7;
        // set timestamp to the end of the stream.
        if (*ts < stream->Times[6]) *ts = stream->Times[6];
        return false;
    }
    *ret = *stream->ReadPtr;
    stream->ReadPtr += 4;
    return true;
}

// always 32 bit
u32 ARM9_InstrRead(struct ARM946ES* ARM9, const u32 addr)
{
    // itcm
    if (ARM9_ITCMTryRead(ARM9, addr))
    {
        // TODO: add contention cycle
        ARM9_FetchCycles(ARM9, 1);
        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize);
    }

    // TODO: icache lookup

    // TODO: run write buffer here (not drain)
    // instruction accesses never drain write buffer.

    // external bus
    bool seq = false; // instruction accesses are always nonsequential
    u32 ret = ARM9_AHBRead(ARM9, &ARM9->ARM.Timestamp, addr, u32_max, false, &seq);
    ARM9_FetchCycles(ARM9, 0); // add dummy cycles to ensure things stay coherent.
    return ret;
}

// arm
void ARM9_InstrRead32(struct ARM946ES* ARM9, const u32 addr)
{
    u32 ret;
    if (ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, &ret, ARM9->ARM.CodeSeq))
    {
        // add dummy cycles to ensure coherency
        ARM9_FetchCycles(ARM9, 0);

        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = ret,
                                                .Aborted = false,
                                                .CoprocPriv = ARM9->ARM.Privileged,
                                                .Flushed = false};
        return;
    }

    // TODO: prefetch abort
    // CHECKME: is this before or after cache streaming is checked for
    if (false)
    {
        ARM9_FetchCycles(ARM9, 1);

        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = 0xE1200070, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                                .Aborted = true,
                                                .CoprocPriv = false, // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
                                                .Flushed = false};
    }
    else
    {
        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = ARM9_InstrRead(ARM9, addr),
                                                .Aborted = false,
                                                .CoprocPriv = ARM9->ARM.Privileged,
                                                .Flushed = false};
    }
}

// thumb
void ARM9_InstrRead16(struct ARM946ES* ARM9, const u32 addr)
{
    u32 instr;
    if ((addr & 2) && ARM9->ARM.CodeSeq) // note: not 100% sure if this logic is correct but it probably doesn't matter...?
    {
        // CHECKME: can this prefetch abort somehow?

        ARM9_FetchCycles(ARM9, 1);

        // use latched halfword
        instr = ARM9->LatchedHalfword;
    }
    else if (ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, &instr, ARM9->ARM.CodeSeq))
    {
        // add dummy cycles to ensure coherency
        ARM9_FetchCycles(ARM9, 0);
    }
    else
    {
        // TODO: prefetch abort
        // CHECKME: is this before or after cache streaming is checked for
        if (false)
        {
            ARM9_FetchCycles(ARM9, 1);
            ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = 0xBE00, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                                    .Aborted = true,
                                                    .CoprocPriv = false, // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
                                                    .Flushed = false};
            return;
        }
        else
        {
            instr = ARM9_InstrRead(ARM9, addr);

            if (addr & 2)
            {
                // correct instruction alignment.
                // TODO: I think this is wrong for big endian
                instr >>= 16;
            }
            else
            {
                // save other halfword for later
                // TODO: I think this is wrong for big endian
                // CHECKME: does latching apply to all access types?
                ARM9->LatchedHalfword = (instr >> 16);

                // NOTE: not sure if clearing the high bits is correct?
                // it's not currently clear if it's possible to switch from thumb -> arm without a pipeline flush on the ARM946E-S, but exact behavior might matter for handling that.
                instr &= u16_max;
            }
        }
    }

    ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = instr,
                                            .Aborted = false,
                                            .CoprocPriv = false, // privilege bug shouldn't matter here; thumb does not have coprocessor instructions.
                                            .Flushed = false};
}

u32 ARM9_DataRead(struct ARM946ES* ARM9, const u32 addr, const u32 mask, bool* seq, bool* dabt)
{
    // ldm/stm (and presumably ldrd/strd too) are forcibly split when crossing 4 KiB boundaries to perform a permission look up again.
    // we aren't actually implementing it that way currently but tbf we could?
    // CHECKME: would it make sense to only do this on ahb accesses? Does it impact abort/itcm/dtcm/cache timings?
    if ((addr & (KiB(4)-1)) == 0)
    {
        *seq = false;
    }

    // TODO: CACHE STREAMING HANDLING

    // handle contention
    // CHECKME: does this apply to aborts?
    // CHECKME: This does apply to itcm right?
    if (ARM9->MemTimestamp < ARM9->DataContTS)
        ARM9->MemTimestamp = ARM9->DataContTS;

    if (false) // TODO: Data Aborts
    {
        *dabt = true;
    }
    else if (ARM9_ITCMTryRead(ARM9, addr))
    {
        // TODO: make deferrable...?
        // TODO: cause contention
        ARM9->MemTimestamp += 1;
        *seq = true;
        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize);
    }
    else if (ARM9_DTCMTryRead(ARM9, addr))
    {
        ARM9->MemTimestamp += 1;
        *seq = true;
        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize);
    }
    else if (false)
    {
        // dcache
    }

    if (false)
    {
        // if bufferable then we need to drain write buffer
    }
    else
    {
        // otherwise we simply run the write buffer
    }

    // note: Technically the initial load in SWP(B) is atomic, but I dont think that actually matters in any way?
    // So I dont think we actually need to handle anything here?
    u32 ret = ARM9_AHBRead(ARM9, &ARM9->MemTimestamp /*not quite sure how I want to handle this yet?*/, addr, mask, false, seq);

    *seq = true;
    return ret;
}

u32 ARM9_DataRead32(struct ARM946ES* ARM9, u32 addr, bool* seq, bool* dabt)
{
    return ARM9_DataRead(ARM9, addr, u32_max, seq, dabt);
}

u16 ARM9_DataRead16(struct ARM946ES* ARM9, u32 addr, bool* seq, bool* dabt)
{
    u32 mask = u16_max << ((addr & 2) * 8);
    u32 ret = ARM9_DataRead(ARM9, addr, mask, seq, dabt);

    // note: arm9 ldrh doesn't do a rotate right so we need to special case the correction here.
    return ret >> ((addr & 2) * 8);
}

u8 ARM9_DataRead8(struct ARM946ES* ARM9, u32 addr, bool* seq, bool* dabt)
{
    u32 mask = u8_max << ((addr & 3) * 8);
    return ARM9_DataRead(ARM9, addr, mask, seq, dabt);
}

void ARM9_DataWrite(struct ARM946ES* ARM9, u32 addr, const u32 val, const u32 mask, const bool atomic, const bool deferrable, bool* seq, bool* dabt)
{
    // ldm/stm (and presumably ldrd/strd too) are forcibly split when crossing 4 KiB boundaries to perform a permission look up again.
    // we aren't actually implementing it that way currently but tbf we could?
    // CHECKME: would it make sense to only do this on ahb accesses? Does it impact abort/itcm/dtcm/cache timings?
    if ((addr & (KiB(4)-1)) == 0)
    {
        *seq = false;
    }

    // TODO: CACHE STREAMING HANDLING

    // handle contention
    // CHECKME: does this apply to aborts?
    // CHECKME: this does actually apply to writes, right?
    // CHECKME: This does apply to itcm right?
    if (ARM9->MemTimestamp < ARM9->DataContTS)
        ARM9->MemTimestamp = ARM9->DataContTS;

    if (false) // TODO: Data Aborts
    {
        *dabt = true;
    }
    else if (ARM9_ITCMTryWrite(ARM9, addr))
    {
        // TODO: make deferrable
        // TODO: cause contention
        // CHECKME: Does this cause data bus contention?
        MemoryWrite(32, ARM9->ITCM, addr, ARM9_ITCMSize, val, mask);
        ARM9->MemTimestamp += 1;
        *seq = true;
        return;
    }
    else if (ARM9_DTCMTryWrite(ARM9, addr))
    {
        MemoryWrite(32, ARM9->DTCM, addr, ARM9_DTCMSize, val, mask);
        ARM9->MemTimestamp += 1;
        ARM9->DataContTS = ARM9->MemTimestamp + 1;
        *seq = true;
        return;
    }
    else if (false)
    {
        // dcache
    }

    if (false)
    {
        // if bufferable then we need to write to the write buffer
    }
    else
    {
        // otherwise we need to drain write buffer
    }

    ARM9_AHBWrite(ARM9, &ARM9->MemTimestamp /*not quite sure how I want to handle this yet?*/, addr, val, mask, atomic, seq);

    *seq = true;
}
