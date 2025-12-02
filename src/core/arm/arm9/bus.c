#include "arm.h"
#include "../../bus/ahb.h"
#include <stdbit.h>




bool ARM9_ITCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !ARM9->CP15.CR.ITCMLoadMode && !((u64)addr >> ARM9->CP15.ITCMShift));
}

bool ARM9_ITCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !((u64)addr >> ARM9->CP15.ITCMShift));
}

bool ARM9_DTCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return (((u64)addr >> ARM9->CP15.DTCMShift) == ARM9->CP15.DTCMReadBase);
}

bool ARM9_DTCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return (((u64)addr >> ARM9->CP15.DTCMShift) == ARM9->CP15.DTCMWriteBase);
}

struct ARM9_MPUPerms ARM9_RegionLookup(const struct ARM946ES* ARM9, const u32 addr, const bool priv)
{
    for (int i = 7; i >= 0; i--)
    {
        if ((addr & ARM9->CP15.MPURegionMask[i]) == ARM9->CP15.MPURegionBase[i])
            return (priv ? ARM9->CP15.MPURegionPermsPriv[i] : ARM9->CP15.MPURegionPermsUser[i]);
    }
    return (struct ARM9_MPUPerms) {.Read = false, .Write = false, .Exec = false, .ICache = false, .DCache = false, .Buffer = false};
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
    u32 ret = AHB9_Read(ARM9->ARM.Sys, ts, addr, mask, atomic, false, seq, true);
    ARM9->LastBusTime = *ts;

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
    AHB9_Write(ARM9->ARM.Sys, ts, addr, val, mask, atomic, seq, true);
    ARM9->LastBusTime = *ts;

    // convert clock back
    // the arm9 interacts with the bus on the rising edge of the bus clock so we get the result on the first cycle of the clock.
    // so we do this weird looking thing to get the right effect.
    *ts = ((*ts - 1) << ((ARM9->BoostedClock) ? 2 : 1)) + 1;
}

#define wb

void ARM9_RunWriteBuffer(struct ARM946ES* ARM9)
{
    struct ARM9_WriteBuffer* buf = &ARM9->WBuffer;

    // check latched fifo data.
    if (buf->Latched)
    {
        /*if (buf->AddrLatched)
        {
            buf->CurAddr = buf->NextAddr;
            buf->AddrLatched = false;
            buf->BufferSeq = false;
        }*/
        u32 mask = 0;
        u32 val = buf->FIFOEntry[16].Data;
        u8 flag = buf->FIFOEntry[16].Flags;
        if (flag == A9WB_8)
        {
            mask = u8_max << ((buf->CurAddr & 0x3) * 8);
        }
        else if (flag == A9WB_16)
        {
            mask = u16_max << ((buf->CurAddr & 0x2) * 8);
        }
        else if (flag == A9WB_32)
        {
            mask = u32_max;
        }
        else CrashSpectacularly("EXPLOSION\n");

        ARM9_AHBWrite(ARM9, &buf->NextStep, buf->CurAddr, val, mask, false, &buf->BufferSeq);
        buf->BufferSeq = true;
        // increment latched address
        // this is always correct in practice since the arm9 doesn't support 8/16 bit sequentials
        buf->CurAddr += 4;
        buf->Latched = false;
    }

    // check if buffer is empty *now*
    if (buf->FIFOFillPtr != 16)
    {
        // CHECKME: do these actually both fetch in 1 cycle? probably not, right?
        // latch new values
        if (buf->FIFOEntry[buf->FIFODrainPtr].Flags == A9WB_Addr)
        {
            buf->CurAddr = buf->FIFOEntry[buf->FIFODrainPtr].Data;
            //buf->AddrLatched = true;
            buf->NextStep++; // this is probably wrong but shouldn't matter i think?
            buf->BufferSeq = false;
            buf->Latched = false;
        }
        else
        {
            buf->FIFOEntry[16].Data = buf->FIFOEntry[buf->FIFODrainPtr].Data;
            buf->FIFOEntry[16].Flags = buf->FIFOEntry[buf->FIFODrainPtr].Flags;
            buf->Latched = true;
        }
        buf->FIFODrainPtr = (buf->FIFODrainPtr + 1) % 16;

        // indicate fifo empty if empty
        if (buf->FIFODrainPtr == buf->FIFOFillPtr)
            buf->FIFOFillPtr = 16;
    }
}

void ARM9_CatchUpWriteBuffer(struct ARM946ES* ARM9, timestamp* until)
{
#ifdef wb
    struct ARM9_WriteBuffer* buf = &ARM9->WBuffer;

    while((buf->FIFOFillPtr != 16 || buf->Latched) && ((*until > buf->NextStep) || buf->BufferSeq))
    {
        ARM9_RunWriteBuffer(ARM9);
    }
#endif
}

void ARM9_DrainWriteBuffer(struct ARM946ES* ARM9, timestamp* until)
{
#ifdef wb
    struct ARM9_WriteBuffer* buf = &ARM9->WBuffer;

    // loop until write buffer is empty
    while(buf->FIFOFillPtr != 16 || buf->Latched)
    {
        ARM9_RunWriteBuffer(ARM9);
    }
    if (*until < buf->NextStep)
        *until = buf->NextStep;
#endif
}

void ARM9_FillWriteBuffer(struct ARM946ES* ARM9, timestamp* now, u32 val, u8 flag)
{
    struct ARM9_WriteBuffer* buf = &ARM9->WBuffer;

#ifdef wb
    // is fifo full?
    if (buf->FIFOFillPtr == buf->FIFODrainPtr)
    {
        while(buf->FIFOFillPtr == buf->FIFODrainPtr)
        {
            ARM9_RunWriteBuffer(ARM9);
        }
        if (*now < buf->NextStep)
            *now = buf->NextStep;
    }
    else
    {
        ARM9_CatchUpWriteBuffer(ARM9, now);
    }

    // if fifo was empty reinitialize the ptrs and update timestamp
    if (buf->FIFOFillPtr == 16)
    {
        if (buf->NextStep < *now)
            buf->NextStep = *now;

        buf->FIFOFillPtr = 0;
        buf->FIFODrainPtr = 0;
    }

    buf->FIFOEntry[buf->FIFOFillPtr].Data = val;
    buf->FIFOEntry[buf->FIFOFillPtr].Flags = flag;

    buf->FIFOFillPtr = (buf->FIFOFillPtr + 1) % 16;
#else
    if (flag == A9WB_Addr)
    {
        buf->CurAddr = val;
    }
    else
    {
        u32 mask;
        if (flag == A9WB_8)
        {
            mask = u8_max << ((buf->CurAddr & 0x3) * 8);
        }
        else if (flag == A9WB_16)
        {
            mask = u16_max << ((buf->CurAddr & 0x2) * 8);
        }
        else if (flag == A9WB_32)
        {
            mask = u32_max;
        }
        else CrashSpectacularly("EXPLOSION\n");
        ARM9_AHBWrite(ARM9, now, buf->CurAddr, val, mask, false, &buf->BufferSeq);
        buf->CurAddr+=4;
    }
#endif
}

// xorshift algorithm i stole from wikipedia:
// https://en.wikipedia.org/wiki/Xorshift
// obvious but this is not even close to how this works.
u64 ARM9_CachePRNG(u64* input)
{
	u64 x = *input;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return *input = x;
}

u32 ARM9_ICacheLookup(struct ARM946ES* ARM9, const u32 addr)
{
    ARM9_ICacheSetLookup

    // if we found a valid set we use that set.
    if (set < ARM9_ICacheAssoc)
    {
        // use set to lookup into icache

        if (ARM9->ARM.Timestamp < ARM9->InstrContTS)
            ARM9->ARM.Timestamp += 1;

        ARM9_FetchCycles(ARM9, 1);
        ARM9->InstrContTS = ARM9->ARM.Timestamp;

        return ARM9->ICache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)];
    }

    // cache line fill time, oh boy.

    // roll for a set
    if (ARM9->CP15.CR.CacheRR)
    {
        ARM9->CP15.ICachePRNG += 1; // CHECKME: how does this actually tick the prng?
        set = ARM9->CP15.ICachePRNG & 3;
    }
    else set = ARM9_CachePRNG(&ARM9->CP15.ICachePRNG) & 3;

    // update tag ram
    // CHECKME: is this actually done immediately?
    ARM9->ITagRAM[index+set].Valid = true;
    ARM9->ITagRAM[index+set].TagBits = (tagcmp >> 1);

    // begin cache streaming
    ARM9_CatchUpWriteBuffer(ARM9, &ARM9->ARM.Timestamp);

    // CHECKME: can you read from icache mid-stream with test commands?
    bool seq = false;
    timestamp time = ARM9->ARM.Timestamp;
    unsigned waittil = (addr / sizeof(u32)) & 0x7;
    u32 actualaddr = addr & ~0x1F;
    for (unsigned i = 0; i < 8; i++)
    {
        // TODO: Atomic access bug
        ARM9->ICache.b32[((index | set)<<3) + i] = ARM9_AHBRead(ARM9, &time, actualaddr+(i*4), u32_max, false, &seq);
        seq = true;
        if (waittil == i)
        {
            ARM9->ARM.Timestamp = time;
            ARM9->IStream.ReadPtr = &ARM9->ICache.b32[((index | set)<<3) | (i+1)];
            ARM9->IStream.Prog = i;
        }
        else if (waittil > 0)
        {
            ARM9->IStream.Times[i] = time;
        }
    }
    ARM9_FetchCycles(ARM9, 0); // dummy to ensure coherency.
    return ARM9->ICache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)];
}

void DCache_CleanLine(struct ARM946ES* ARM9, const u32 idxset);

u32 ARM9_DCacheReadLookup(struct ARM946ES* ARM9, const u32 addr)
{
    ARM9_DCacheSetLookup

    // if we found a valid set we use that set.
    if (set < ARM9_DCacheAssoc)
    {
        // use set to lookup into icache
        ARM9->MemTimestamp += 1;
        u32 val = ARM9->DCache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)];
        return val;
    }

    // cache line fill time, oh boy.

    // roll for a set
    if (ARM9->CP15.CR.CacheRR)
    {
        ARM9->CP15.DCachePRNG += 1; // CHECKME: how does this actually tick the prng?
        set = ARM9->CP15.DCachePRNG & 3;
    }
    else set = ARM9_CachePRNG(&ARM9->CP15.DCachePRNG) & 3;

    // CHECKME: is it clean -> fill or fill -> clean?
    // i would assume the former? because the other sounds hard to implement

    // CHECKME: can this trigger the clean+flush errata
    DCache_CleanLine(ARM9, index|set);

    // update tag ram
    // CHECKME: is this actually done immediately?
    ARM9->DTagRAM[index|set].Valid = true;
    ARM9->DTagRAM[index|set].TagBits = (tagcmp >> 1);

    // begin cache streaming
    ARM9_DrainWriteBuffer(ARM9, &ARM9->MemTimestamp);

    // CHECKME: can you read from icache mid-stream with test commands?
    bool seq = false;
    timestamp time = ARM9->MemTimestamp;
    unsigned waittil = (addr / sizeof(u32)) & 0x7;
    u32 actualaddr = addr & ~0x1F;
    for (unsigned i = 0; i < 8; i++)
    {
        // TODO: Atomic access bug
        ARM9->DCache.b32[((index | set)<<3) + i] = ARM9_AHBRead(ARM9, &time, actualaddr+(i*4), u32_max, false, &seq);
        seq = true;
        if (waittil == i)
        {
            ARM9->MemTimestamp = time;
            ARM9->DStream.ReadPtr = &ARM9->DCache.b32[((index | set)<<3) | (i+1)];
            ARM9->DStream.Prog = i;
        }
        else if (waittil < i)
        {
            ARM9->DStream.Times[i] = time;
        }
    }
    return ARM9->DCache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)];
}

bool ARM9_DCacheWriteLookup(struct ARM946ES* ARM9, const u32 addr, const u32 val, const u32 mask, const bool bufferable)
{
    ARM9_DCacheSetLookup

    // if we found a valid set we use that set.
    if (set < ARM9_DCacheAssoc)
    {
        // use set to lookup into icache

        // CHECKME: does this actually contention?
        ARM9->DataContTS = ARM9->MemTimestamp + 1;

        MaskedWrite(ARM9->DCache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)], val, mask);

        if (bufferable)
        {
            ARM9->MemTimestamp += 1;
            if (addr & 0x10)
                ARM9->DTagRAM[index | set].DirtyHi = true;
            else
                ARM9->DTagRAM[index | set].DirtyLo = true;

            return true;
        }
        // fall through to writebuffer
    }
    return false;
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

    if (*ts < stream->Times[stream->Prog])
        *ts = stream->Times[stream->Prog];

    *ret = *stream->ReadPtr;
    stream->ReadPtr += 4;
    stream->Prog += 1;
    return true;
}

// CHECKME: need better research on how p. abt, cache streaming, and ahb accesses interact with i.bus contention and data itcm deference

// always 32 bit
u32 ARM9_InstrRead(struct ARM946ES* ARM9, const u32 addr, const struct ARM9_MPUPerms perms)
{
    // itcm
    if (ARM9_ITCMTryRead(ARM9, addr))
    {
        if (ARM9->ARM.Timestamp < ARM9->InstrContTS)
            ARM9->ARM.Timestamp += 1;

        ARM9_FetchCycles(ARM9, 1);
        ARM9->InstrContTS = ARM9->ARM.Timestamp;

        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize);
    }

    if (perms.ICache)
    {
        return ARM9_ICacheLookup(ARM9, addr);
    }

    // TODO: run write buffer here (not drain)
    // instruction accesses never drain write buffer.
    ARM9_CatchUpWriteBuffer(ARM9, &ARM9->ARM.Timestamp);

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

    const struct ARM9_MPUPerms perms = ARM9_RegionLookup(ARM9, addr, ARM9->ARM.Privileged);
    // TODO: prefetch abort
    // CHECKME: is this before or after cache streaming is checked for
    if (!perms.Exec)
    {
        ARM9_Log(ARM9);
        ARM9_FetchCycles(ARM9, 1);

        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = 0xE1200070, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                                .Aborted = true,
                                                .CoprocPriv = false, // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
                                                .Flushed = false};
    }
    else
    {
        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = ARM9_InstrRead(ARM9, addr, perms),
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

        if (ARM9->ARM.Timestamp < ARM9->InstrContTS)
            ARM9->ARM.Timestamp += 1;

        ARM9_FetchCycles(ARM9, 1);
        ARM9->InstrContTS = ARM9->ARM.Timestamp;

        // use latched halfword
        instr = ARM9->LatchedHalfword;
    }
    else
    {
        const struct ARM9_MPUPerms perms = ARM9_RegionLookup(ARM9, addr, ARM9->ARM.Privileged);
        // CHECKME: is this before or after cache streaming is checked for
        if (!perms.Exec)
        {
            ARM9_Log(ARM9);
            ARM9_FetchCycles(ARM9, 1);
            ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = 0xBE00, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                                    .Aborted = true,
                                                    .CoprocPriv = false, // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
                                                    .Flushed = false};
            return;
        }
        else
        { 
            if (ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, &instr, ARM9->ARM.CodeSeq))
            {
                // add dummy cycles to ensure coherency
                ARM9_FetchCycles(ARM9, 0);
            }
            else instr = ARM9_InstrRead(ARM9, addr, perms);

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

    u32 ret;
    if (ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, &ret, *seq))
    {
        return ret;
    }

    // handle contention
    // CHECKME: does this apply to aborts?
    // CHECKME: This does apply to itcm right?
    if (ARM9->MemTimestamp < ARM9->DataContTS)
        ARM9->MemTimestamp = ARM9->DataContTS;

    const struct ARM9_MPUPerms perms = ARM9_RegionLookup(ARM9, addr, ARM9->ARM.Privileged);
    if (!perms.Read)
    {
        ARM9_Log(ARM9);
        LogPrint(LOG_ARM9|LOG_EXCEP, "DATA ABORT: READ FROM: %08X\n", addr);
        ARM9->MemTimestamp += 1;
        *dabt = true;
        *seq = true;
        return 0;
    }
    else if (ARM9_ITCMTryRead(ARM9, addr))
    {
        // TODO: make deferrable...?
        if (ARM9->MemTimestamp <= ARM9->InstrContTS)
            ARM9->MemTimestamp += 1;

        ARM9->MemTimestamp += 1;
        ARM9->InstrContTS = ARM9->MemTimestamp;
        *seq = true;
        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize) & mask;
    }
    else if (ARM9_DTCMTryRead(ARM9, addr))
    {
        ARM9->MemTimestamp += 1;
        *seq = true;
        return MemoryRead(32, ARM9->DTCM, addr, ARM9_DTCMSize) & mask;
    }
    else if (perms.DCache)
    {
        ret = ARM9_DCacheReadLookup(ARM9, addr);
        *seq = true;
        return ret & mask;
    }

    if (perms.DCache || perms.Buffer)
    {
        // if bufferable then we need to drain write buffer
        ARM9_DrainWriteBuffer(ARM9, &ARM9->MemTimestamp);
    }
    else
    {
        // otherwise we simply run the write buffer
        ARM9_CatchUpWriteBuffer(ARM9, &ARM9->MemTimestamp);
    }

    // note: Technically the initial load in SWP(B) is atomic, but I dont think that actually matters in any way?
    // So I dont think we actually need to handle anything here?
    ret = ARM9_AHBRead(ARM9, &ARM9->MemTimestamp, addr, mask, false, seq);

    *seq = true;
    return ret & mask;
}

u32 ARM9_DataRead32(struct ARM946ES* ARM9, u32 addr, bool* seq, bool* dabt)
{
    return ARM9_DataRead(ARM9, addr, u32_max, seq, dabt);
}

u16 ARM9_DataRead16(struct ARM946ES* ARM9, u32 addr, bool* seq, bool* dabt)
{
    u32 mask = ROL32(u16_max,((addr & 2) * 8));
    u32 ret = ARM9_DataRead(ARM9, addr, mask, seq, dabt);

    // note: arm9 ldrh doesn't do a rotate right so we need to special case the correction here.
    return ret >> ((addr & 2) * 8);
}

u32 ARM9_DataRead8(struct ARM946ES* ARM9, u32 addr, bool* seq, bool* dabt)
{
    u32 mask = ROL32(u8_max, ((addr & 3) * 8));
    u32 ret = ARM9_DataRead(ARM9, addr, mask, seq, dabt);

    return ret;
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

    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);

    // handle contention
    // CHECKME: does this apply to aborts?
    // CHECKME: this does actually apply to writes, right?
    // CHECKME: This does apply to itcm right?
    //if (ARM9->MemTimestamp < ARM9->DataContTS)
    //    ARM9->MemTimestamp = ARM9->DataContTS;

    const struct ARM9_MPUPerms perms = ARM9_RegionLookup(ARM9, addr, ARM9->ARM.Privileged);
    if (!perms.Write)
    {
        ARM9_Log(ARM9);
        LogPrint(LOG_ARM9|LOG_EXCEP, "DATA ABORT: WRITE TO: %08X\n", addr);
        ARM9->MemTimestamp += 1;
        *dabt = true;
        *seq = true;
        return;
    }
    else if (ARM9_ITCMTryWrite(ARM9, addr))
    {
        // itcm writes (and presumably reads too but those matter less) are reordered after the arm9 instruction bus goes;
        // this doesn't apply to instructions like ldm/stm since those start sequential burst and the instr read can't begin until later on in the instruction
        // (internal buses wont interrupt other internal buses)
        // this does apply to swp interestingly enough.
        if (deferrable)
        {
            ARM9->DeferredWrite = true;
            ARM9->DeferredAddr = addr; // this truncates the addr, but that's fine.
            ARM9->DeferredVal = val;
            ARM9->DeferredMask = mask;
            *seq = true;
            return;
        }
        else
        {
            //if (ARM9->MemTimestamp <= ARM9->InstrContTS)
            //    ARM9->MemTimestamp += 1;

            MemoryWrite(32, ARM9->ITCM, addr, ARM9_ITCMSize, val, mask);
            //ARM9->MemTimestamp += 1;
            *seq = true;
            return;
        }
    }
    else if (ARM9_DTCMTryWrite(ARM9, addr))
    {
        MemoryWrite(32, ARM9->DTCM, addr, ARM9_DTCMSize, val, mask);

        ARM9->MemTimestamp += 1;
        ARM9->DataContTS = ARM9->MemTimestamp + 1;
        *seq = true;
        return;
    }
    else if (perms.DCache && ARM9_DCacheWriteLookup(ARM9, addr, val, mask, perms.Buffer))
    {
        *seq = true;
        return;
    }

    // atomic flag checked for since swp doesn't use the write buffer.
    if ((perms.DCache || perms.Buffer) && !atomic)
    {
        u32 size = stdc_count_ones(mask);
        if (size == 8)
        {
            size = A9WB_8;
        }
        else if (size == 16)
        {
            size = A9WB_16;
        }
        else if (size == 32)
        {
            size = A9WB_32;
        }
        else CrashSpectacularly("%08X\n", mask);

        ARM9->DataContTS = ARM9->MemTimestamp + 1;
        // if bufferable then we need to write to the write buffer
        ARM9_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, addr, A9WB_Addr);
        ARM9_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, val, size);
    }
    else
    {
        // otherwise we need to drain write buffer
        ARM9_DrainWriteBuffer(ARM9, &ARM9->MemTimestamp);
        ARM9_AHBWrite(ARM9, &ARM9->MemTimestamp, addr, val, mask, atomic, seq);
    }

    *seq = true;
}

void ARM9_DeferredITCMWrite(struct ARM946ES* ARM9)
{
    if (!ARM9->DeferredWrite) return;
    // CHECKME: Does this cause data bus contention too?
    MemoryWrite(32, ARM9->ITCM, ARM9->DeferredAddr, ARM9_ITCMSize, ARM9->DeferredVal, ARM9->DeferredMask);

    //timestamp old = ARM9->MemTimestamp;
    //if (ARM9->MemTimestamp <= ARM9->InstrContTS)
    //    ARM9->MemTimestamp += 1;

    //ARM9->MemTimestamp += 1;

    ARM9->DeferredWrite = false;

    // these probably need to run again
    // jakly why did you make the timing logic so convoluted and unintuitive?
    //ARM9_UpdateInterlocks(ARM9, ARM9->MemTimestamp - old);
    //ARM9_FetchCycles(ARM9, 0);
}
