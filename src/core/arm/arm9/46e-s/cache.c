#include "core/arm/shared/arm.h"
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

    A9ES_InstrBusy(a946);
    // setup biu for cache streaming
    a946->BIU.InstrAddr = addr & ~0x1F; // a94646E-S does not implement wrapping bursts; must always start at beginning of cacheline
    a946->BIU.InstrType = A946BIU_InstrCache;
    a946->BIU.InstrMax = 8;
    a946->BIU.InstrSubmCur = 0;
    a946->BIU.InstrCompCur = 0;

    a946->IStreamWaitCur = ((addr/4) & 0x7) + 1;
    a946->IStreamPtr = (index+set) * A946_ICacheLineLength;
    A946_BIUSched(a946, now);
    return false;
}

// CHECKME: does flushing cache clean the tag ram and/or cache line entirely?
void A946_ICacheFlushAddr(ARM946ES* a946, u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_ICacheSetLookup
    if (set < A946_ICacheAssoc)
        a946->ITagRAM[index+set].Valid = false;
}

void A946_ICacheFlushAll(ARM946ES* a946)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    for (u32 i = 0; i < countof(a946->ITagRAM); i++)
        a946->ITagRAM[i].Valid = false;
}

void A946_ICachePrefetch(ARM946ES* a946, const u32 addr, timestamp now)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    u32 dummy;
    A946_ICacheLookup(a946, addr, now /* idfk how to pass this along */, &dummy);
    a946->IStreamWaitCur = 0; // hacky; override icache stream wait
    a946->ARM.CodeSeq = false; // hacky
}

void A946_DCacheReadLookup(ARM946ES* a946, const AHB_HPROT prot, const u32 addr, const timestamp now, const u8 numfetch)
{
    A946_DCacheSetLookup

    // if we found a valid set use that set to lookup into dcache
    if (set < A946_DCacheAssoc)
    {
        u32 cachebase = ((index | set)<<3) | ((addr/4) & 0x7);
        for (u8 i = 0; i < numfetch; i++)
            a946->PostMem.RData[i+a946->PostMem.NumFetchCompleted] = a946->DCache.b32[cachebase + i];

        a946->DataTS = now + DSClk67(numfetch);
        a946->PostMem.NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);

        return;
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
    // i would assume the former? because the latter sounds harder to implement

    // CHECKME: this likely cannot trigger the clean+flush errata; presumably just does a clean operation and then overwrites the old cacheline
    A946_DCacheCleanLine(a946, now, index|set, false);

    // update tag ram
    // CHECKME: is this actually done immediately?
    a946->DTagRAM[index|set].Valid = true;
    a946->DTagRAM[index|set].TagBits = (tagcmp >> 1);


    // progress memory transfer
    a946->PostMem.DataPtr = a946->PostMem.NumFetchCompleted;
    a946->PostMem.NumFetchCompleted += numfetch;
    A9ES_DataBusy(a946);

    // biu req
    a946->BIU.DataAddr = addr & ~0x1F; // a94646E-S does not implement wrapping bursts; must always start at beginning of cacheline
    a946->BIU.DataType = A946BIU_DataCache;
    a946->BIU.DataMax = 8;
    a946->BIU.DataSubmCur = 0;
    a946->BIU.DataCompCur = 0;
    a946->BIU.DataProt = prot;
    a946->BIU.DataWidth = ARMDataWidth_32;

    // cache streaming vars
    a946->DStreamWaitCur = ((addr/4) & 0x7) + 1;
    a946->DStreamWaitEnd = ((addr/4) & 0x7) + numfetch;
    a946->DStreamPtr = (index+set) * A946_DCacheLineLength;

    A946_BIUSched(a946, now);
}

bool A946_DCacheWriteLookup(ARM946ES* a946, const u32 addr, const timestamp now, const u32 wrlanes, const u8 numfetch, const bool bufferable)
{
    A946_DCacheSetLookup

    // if we found a valid set we use that set.
    if (set < A946_DCacheAssoc)
    {
        // use set to lookup into dcache
        u32 dcachebase = ((index | set)<<3) | ((addr/4) & 0x7);

        if (wrlanes != 0xFFFFFFFF) // handle halfword/byte writes
            MaskedWrite(a946->DCache.b32[dcachebase], a946->PostMem.WrData[a946->PostMem.NumFetchCompleted], wrlanes);
        else for (u8 i = 0; i < numfetch; i++)
            a946->DCache.b32[dcachebase+i] = a946->PostMem.WrData[i+a946->PostMem.NumFetchCompleted];

        if (bufferable) // write-back cache: does not write back to memory until line is cleaned
        {
            if (addr & 0x10) a946->DTagRAM[index|set].DirtyHi = true;
            else             a946->DTagRAM[index|set].DirtyLo = true;

            a946->PostMem.NumFetchCompleted += numfetch;
            a946->DataTS = now + DSClk67(numfetch);
            a946->DataWrStall = a946->DataTS+DSClk67(1);
            A9ES_DataDone(a946);
            return true;
        }
        // write-through cache: write is put into write buffer immediately
    }
    // fall through to writebuffer
    return false;
}

void A946_DCacheFlushAddr(ARM946ES* a946, u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_DCacheSetLookup
    if (set < A946_DCacheAssoc)
        a946->DTagRAM[index+set].Valid = false;
}

void A946_DCacheFlushAll(ARM946ES* a946)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    for (u32 i = 0; i < countof(a946->DTagRAM); i++)
        a946->DTagRAM[i].Valid = false;
}

void A946_DCacheCleanLine(ARM946ES* a946, timestamp now, const u32 idxset, const bool cp15)
{
    const u32 addr = (a946->DTagRAM[idxset].TagBits << 10) | (idxset >> 2 << 5);

    u8 num = 0;
    u32 baseaddr;
    u32* buf;
    if (a946->DTagRAM[idxset].DirtyHi)
    {
        baseaddr = addr + (A946_DCacheLineBytes/2);
        buf = &a946->DCache.b32[(idxset*A946_DCacheLineLength)+4];
        num += 4;
    }

    if (a946->DTagRAM[idxset].DirtyLo)
    {
        baseaddr = addr;
        buf = &a946->DCache.b32[idxset*A946_DCacheLineLength];
        num += 4;
    }

    if (num == 0) return; // already clean

    A946_WriteBufferFill(a946, now, buf, baseaddr, ARMDataWidth_32, num, cp15 ? A946WBCause_CP15 : A946WBCause_DCache);
    a946->DTagRAM[idxset].DirtyLo = false;
    a946->DTagRAM[idxset].DirtyHi = false;
}

void A946_DCacheCleanFlushLine(ARM946ES* a946, timestamp now, const u32 idxset)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING

    // CHECKME: does this errata emulation all check out?
    if (a946->DTagRAM[idxset].DirtyLo || a946->DTagRAM[idxset].DirtyHi)
    {
        A946_DCacheCleanLine(a946, now, idxset, true);
        a946->DTagRAM[idxset].Valid = false;
    }
    else if (!a946->BIU.WBuffer.Full)
    {
        a946->DTagRAM[idxset].Valid = false;
    }
    else
    {
        // when the write buffer is full and the line is already clean the line will not properly be marked invalid
        LogPrint(LOG_ARM9|LOG_BUG, "ARM9 ERRATA TRIGGERED: DCACHE CLEAN+FLUSH FAILED TO FLUSH CLEAN LINE DUE TO FULL WRITE BUFFER!\n");
    }
}

void A946_DCacheCleanIdxSet(ARM946ES* a946, timestamp now, const u32 val)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    u32 idxset = (val >> 30) | (((val >> 5) & 0x1F) << 2);

    A946_DCacheCleanLine(a946, now, idxset, true);
}

void A946_DCacheCleanFlushIdxSet(ARM946ES* a946, timestamp now, const u32 val)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    u32 idxset = (val >> 30) | (((val >> 5) & 0x1F) << 2);

    A946_DCacheCleanFlushLine(a946, now, idxset);
}

void A946_DCacheCleanAddr(ARM946ES* a946, timestamp now, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_DCacheSetLookup
    if (set < A946_DCacheAssoc)
        A946_DCacheCleanLine(a946, now, index | set, true);
}

void A946_DCacheCleanFlushAddr(ARM946ES* a946, timestamp now, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    A946_DCacheSetLookup
    if (set < A946_DCacheAssoc)
        A946_DCacheCleanFlushLine(a946, now, index | set);
}

// TODO: make sure that the arm9 can't be in the future?
void A946_DCacheStream_Post(ARM946ES* a946, u32 rdata, timestamp now)
{
    A9ES_PostMem* post = &a946->PostMem;

    a946->BIU.DataCompCur++;
    a946->DCache.b32[a946->DStreamPtr++] = rdata;

    if (a946->BIU.DataCompCur == a946->DStreamWaitCur) // cpu was waiting for this word!!
    {
        post->RData[post->DataPtr++] = rdata;
        a946->DStreamWaitCur++;
        a946->DataTS = now;

        if (a946->DStreamWaitCur == a946->DStreamWaitEnd) // finished waiting
        {
            a946->DStreamWaitCur = 0;
            if (a946->BIU.WBFill != A946WBCause_DCache)
            {
                A9ES_DataDone(a946);
                Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
            }
        }
    }

    if (a946->BIU.DataCompCur == a946->BIU.DataMax) // stream over
    {
        if (a946->DStreamWaitCur) // let cpu go if it was waiting
        {
            a946->DStreamWaitCur = 0;
            A9ES_DataGo(a946, &a946->PostMem);
            Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
        }

        a946->BIU.BurstCur = A946BIUBurst_None;
        a946->BIU.DataType = A946BIU_DataNone; // free up biu's data path
    }
}

void A946_ICacheStream_Post(ARM946ES* a946, u32 rdata, timestamp now)
{
    a946->ICache.b32[a946->IStreamPtr] = rdata;
    a946->IStreamPtr++;

    if (a946->BIU.InstrCompCur == a946->IStreamWaitCur) // cpu was waiting for this word!!
    {
        // arm946e-s has a fast path for this case
        a946->InstrLatch = rdata;
        a946->InstrTS = now;
        A946_InstrRead_Post(a946, a946->ARM.PC);

        a946->IStreamWaitCur = 0;
        Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
    }

    if (a946->BIU.InstrCompCur == a946->BIU.InstrMax) // stream over
    {
        if (a946->IStreamWaitCur) // let cpu go if it was waiting
        {
            a946->IStreamWaitCur = 0;
            A9ES_InstrGo(a946, false);
            Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
        }
        a946->BIU.InstrType = A946BIU_InstrNone; // free up biu's instr path
    }
}
