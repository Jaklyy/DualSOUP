#include "core/utils.h"
#include "core/scheduler.h"
#include "../arm.h"


// xorshift algorithm i stole from wikipedia:
// https://en.wikipedia.org/wiki/Xorshift
// obvious but this is not even close to how this works.
u64 A946_CachePRNG(u64* input)
{
	u64 x = *input;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return *input = x;
}

bool A946_ICacheLookup(ARM946ES* a946, const u32 addr, timestamp now, u32* instr)
{
    A946_ICacheSetLookup

    // if we found a valid set we use that set.
    if (set < A946_ICacheAssoc)
    {
        // use set to lookup into icache
        *instr = a946->ICache.b32[((index | set)<<3) | ((addr/4) & 0x7)];
        return true;
    }

    // cache line fill time, oh boy.

    // roll for a set
    if (a946->CP15.CR.CacheRR)
    {
        a946->CP15.ICachePRNG += 1; // CHECKME: how does this actually tick the prng?
        set = a946->CP15.ICachePRNG & 3;
    }
    else set = A946_CachePRNG(&a946->CP15.ICachePRNG) & 3;

    // update tag ram
    // CHECKME: is this actually done immediately?
    a946->ITagRAM[index+set].Valid = true;
    a946->ITagRAM[index+set].TagBits = (tagcmp >> 1);

    // setup biu for cache streaming
    a946->BIU.InstrCur = 0;
    a946->BIU.InstrMax = 8;
    a946->BIU.InstrAddr = addr & ~0x1F; // a94646E-S does not implement wrapping bursts; must always start at beginning of cacheline
    a946->BIU.InstrType = A946BIU_InstrCache;
    a946->IStreamWait = ((addr/4) & 0x7) + 1;
    a946->IStreamIndex = (index+set) * A946_ICacheLineLength;
    NeoSched_AddEventIfEarlier(a946->ARM.Sys, now, Evt_ARM9BIU);
    return false;
}

// CHECKME: does flushing cache clean the tag ram and/or cache line entirely?
void A946_ICacheFlushAddr(ARM946ES* a946, u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_ICacheSetLookup
    if (set < A946_ICacheAssoc)
    {
        a946->ITagRAM[index+set].Valid = false;
    }
}

void A946_ICacheFlushAll(ARM946ES* a946)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    for (unsigned i = 0; i < countof(a946->ITagRAM); i++)
        a946->ITagRAM[i].Valid = false;
}

void A946_ICachePrefetch(ARM946ES* a946, const u32 addr, timestamp now)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    u32* dummy;
    A946_ICacheLookup(a946, addr, now /* idfk how to pass this along */, dummy);
    a946->IStreamWait = -1; // hacky; override icache stream wait
}

bool A946_DCacheReadLookup(ARM946ES* a946, const u32 addr, timestamp now, u32* data)
{
    A946_DCacheSetLookup

    // if we found a valid set we use that set.
    if (set < A946_DCacheAssoc)
    {
        // use set to lookup into dcache
        *data = a946->DCache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)];
        return true;
    }

    // cache line fill time, oh boy.

    // roll for a set
    if (a946->CP15.CR.CacheRR)
    {
        a946->CP15.DCachePRNG += 1; // CHECKME: how does this actually tick the prng?
        set = a946->CP15.DCachePRNG & 3;
    }
    else set = A946_CachePRNG(&a946->CP15.DCachePRNG) & 3;

    // CHECKME: is it clean -> fill or fill -> clean?
    // i would assume the former? because the other sounds hard to implement

    // CHECKME: this likely cannot trigger the clean+flush errata; presumably just does a clean operation and then overwrites the old cacheline
    A946_DCacheCleanLine(a946, index|set);

    // update tag ram
    // CHECKME: is this actually done immediately?
    a946->DTagRAM[index|set].Valid = true;
    a946->DTagRAM[index|set].TagBits = (tagcmp >> 1);

    // setup biu for cache streaming
    a946->BIU.DataCur = 0;
    a946->BIU.DataMax = 8;
    a946->BIU.DataAddr = addr & ~0x1F; // a94646E-S does not implement wrapping bursts; must always start at beginning of cacheline
    a946->BIU.DataType = A946BIU_DataCache;
    a946->DStreamWait = ((addr/4) & 0x7) + 1;
    a946->DStreamIndex = index+set;
    NeoSched_AddEventIfEarlier(a946->ARM.Sys, now, Evt_ARM9BIU);
    return false;
}

bool A946_DCacheWriteLookup(ARM946ES* a946, const u32 addr, const u32 val, const u32 mask, const bool bufferable)
{
    A946_DCacheSetLookup

    // if we found a valid set we use that set.
    if (set < A946_DCacheAssoc)
    {
        // use set to lookup into icache

        // CHECKME: does this actually contention?
        a946->DataContTS = a946->MemTimestamp + 1;

        MaskedWrite(a946->DCache.b32[((index | set)<<3) | ((addr/sizeof(u32)) & 0x7)], val, mask);

        if (bufferable)
        {
            a946->MemTimestamp += 1;
            if (addr & 0x10)
                a946->DTagRAM[index | set].DirtyHi = true;
            else
                a946->DTagRAM[index | set].DirtyLo = true;

            return true;
        }
        // fall through to writebuffer
    }
    return false;
}

void A946_DCacheFlushAddr(ARM946ES* a946, u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_DCacheSetLookup
    if (set < A946_DCacheAssoc)
    {
        a946->DTagRAM[index+set].Valid = false;
    }
}

void A946_DCacheFlushAll(ARM946ES* a946)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    for (unsigned i = 0; i < countof(a946->DTagRAM); i++)
        a946->DTagRAM[i].Valid = false;
}

void A946_DCacheCleanLine(ARM946ES* a946, const u32 idxset)
{
    bool seq = false;
    u32 addr = (a946->DTagRAM[idxset].TagBits << 10) | (idxset >> 2 << 5);
    static_assert(false, "REDO WRITE BUFFER HANDLING\n");
    if (a946->DTagRAM[idxset].DirtyLo)
    {
        //A946_FillWriteBuffer(a946, &a946->MemTimestamp, addr, A9WB_Addr);
        seq = true;
        for (int i = 0; i < 4; i++)
        {
            //A946_FillWriteBuffer(a946, &a946->MemTimestamp, a946->DCache.b32[(idxset<<3)+i], A9WB_32);
        }
    }
    if (a946->DTagRAM[idxset].DirtyHi)
    {
        if (!seq)
            //A946_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, addr+(4*sizeof(u32)), A9WB_Addr);
        for (int i = 4; i < 8; i++)
        {
            //A946_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, ARM9->DCache.b32[(idxset<<3)+i], A9WB_32);
        }
    }
    a946->DTagRAM[idxset].DirtyLo = false;
    a946->DTagRAM[idxset].DirtyHi = false;
}

void A946_DCacheCleanFlushLine(ARM946ES* a946, const u32 idxset)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING

    // CHECKME: does this errata emulation all check out?
    if (a946->DTagRAM[idxset].DirtyLo || a946->DTagRAM[idxset].DirtyHi)
    {
        A946_DCacheCleanLine(a946, idxset);
        a946->DTagRAM[idxset].Valid = false;
    }
    else if (a946->WBuffer.FIFOFillPtr != a946->WBuffer.FIFODrainPtr)
    {
        static_assert(false, "REDO WRITE BUFFER HANDLING\n");
        a946->DTagRAM[idxset].Valid = false;
    }
    else
    {
        // when the write buffer is full and the line is already clean the line will not properly be marked invalid
        LogPrint(LOG_ARM9|LOG_BUG, "ARM9 ERRATA TRIGGERED: DCACHE CLEAN+FLUSH FAILED TO FLUSH CLEAN LINE DUE TO FULL WRITE BUFFER!\n");
    }
}

void A946_DCacheCleanIdxSet(ARM946ES* a946, const u32 val)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    u32 idxset = (val >> 30) | (((val >> 5) & 0x1F) << 2);

    A946_DCacheCleanLine(a946, idxset);
}

void A946_DCacheCleanFlushIdxSet(ARM946ES* a946, const u32 val)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    u32 idxset = (val >> 30) | (((val >> 5) & 0x1F) << 2);

    A946_DCacheCleanFlushLine(a946, idxset);
}

void A946_DCacheCleanAddr(ARM946ES* a946, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_DCacheSetLookup
    if (set < A946_DCacheAssoc)
    {
        A946_DCacheCleanLine(a946, index | set);
    }
}

void A946_DCacheCleanFlushAddr(ARM946ES* a946, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_DCacheSetLookup
    if (set < A946_DCacheAssoc)
    {
        A946_DCacheCleanFlushLine(a946, index | set);
    }
}

// TODO: make sure that the arm9 can't be in the future?
void A946_DCacheStream_Post(ARM946ES* a946, u32 val, timestamp now)
{
    a946->DCache.b32[a946->DStreamIndex] = val;
    a946->DStreamIndex++;

    if (a946->BIU.DataCur == a946->DStreamWait) // cpu was waiting for this word!!
    {
        a946->DStreamWait = 0;
        NeoSched_AddEvent(a946->ARM.Sys, NTRClock_CvtFrom67(now), Evt_ARM9);
    }
    if (a946->BIU.DataCur == a946->BIU.DataMax) // stream over
    {
        if (a946->DStreamWait) // let cpu go if it was waiting
        {
            a946->DStreamWait = 0;
            NeoSched_AddEvent(a946->ARM.Sys, NTRClock_CvtFrom67(now), Evt_ARM9);
        }
        a946->BIU.DataType = A946BIU_DataNone; // free up biu's data path
    }
}

void A946_ICacheStream_Post(ARM946ES* a946, u32 val, timestamp now)
{
    a946->ICache.b32[a946->IStreamIndex] = val;
    a946->IStreamIndex++;

    if (a946->BIU.InstrCur == a946->IStreamWait) // cpu was waiting for this word!!
    {
        // arm946e-s has a fast path for this case
        a946->InstrLatch = val;
        A946_InstrRead_Post(a946, a946->BIU.InstrAddr, now);

        a946->IStreamWait = 0;
        NeoSched_AddEvent(a946->ARM.Sys, NTRClock_CvtFrom67(now), Evt_ARM9);
    }
    if (a946->BIU.InstrCur == a946->BIU.InstrMax) // stream over
    {
        if (a946->IStreamWait) // let cpu go if it was waiting
        {
            a946->IStreamWait = 0;
            NeoSched_AddEvent(a946->ARM.Sys, NTRClock_CvtFrom67(now), Evt_ARM9);
        }
        a946->BIU.InstrType = A946BIU_InstrNone; // free up biu's instr path
    }
}
