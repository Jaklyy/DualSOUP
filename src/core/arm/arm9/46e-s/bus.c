#ifdef __AVX2__
    #include <immintrin.h>
#endif
#include "core/utils.h"
#include "core/bus/ahb.h"
#include "../arm.h"




bool A946_ITCMTryRead(const ARM946ES* a946, const u32 addr)
{
    return (a946->CP15.CR.ITCMEnable && !a946->CP15.CR.ITCMLoadMode && !((u64)addr >> a946->CP15.ITCMShift));
}

bool A946_ITCMTryWrite(const ARM946ES* a946, const u32 addr)
{
    return (a946->CP15.CR.ITCMEnable && !((u64)addr >> a946->CP15.ITCMShift));
}

bool A946_DTCMTryRead(const ARM946ES* a946, const u32 addr)
{
    return (((u64)addr >> a946->CP15.DTCMShift) == a946->CP15.DTCMReadBase);
}

bool A946_DTCMTryWrite(const ARM946ES* a946, const u32 addr)
{
    return (((u64)addr >> a946->CP15.DTCMShift) == a946->CP15.DTCMWriteBase);
}

A946_MPUPerms A946_RegionLookup(const ARM946ES* a946, const u32 addr, const bool priv)
{
#ifdef __AVX2__
    __m256i addrs = _mm256_set1_epi32(addr);
    __m256i masks; memcpy(&masks, a946->CP15.MPURegionMask, sizeof(masks));
    __m256i bases; memcpy(&bases, a946->CP15.MPURegionBase, sizeof(bases));
    addrs = _mm256_and_si256(masks, addrs);
    __m256i res = _mm256_cmpeq_epi32(addrs, bases);
    u8 rgn = stdc_leading_zeros((u32)_mm256_movemask_ps(_mm256_castsi256_ps(res)));
    if (rgn < 32)
        return (priv ? a946->CP15.MPURegionPermsPriv[31-rgn] : a946->CP15.MPURegionPermsUser[31-rgn]);
#else
    for (int i = 7; i >= 0; i--)
    {
        if ((addr & a946->CP15.MPURegionMask[i]) == a946->CP15.MPURegionBase[i])
            return (priv ? a946->CP15.MPURegionPermsPriv[i] : a946->CP15.MPURegionPermsUser[i]);
    }
#endif 
    return (A946_MPUPerms) {.Read = false, .Write = false, .Exec = false, .ICache = false, .DCache = false, .Buffer = false};
}

void A946_InstrFetchAborted(ARM946ES* a946, timestamp now)
{
    // Note: no latching is done here because no instruction fetch was done.
    if (a946->ARM.CPSR.Thumb)
    {
        a946->ARM.Instr[2] = (ARM_Instr){.Raw = 0xBE00, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                         .Aborted = true, // flag used for distinguishing bkpt from actual prefetch aborts.
                                         .CoprocPriv = false}; // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
    }
    else // arm
    {
        a946->ARM.Instr[2] = (ARM_Instr){.Raw = 0xE1200070, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                         .Aborted = true, // flag used for distinguishing bkpt from actual prefetch aborts.
                                         .CoprocPriv = false}; // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
    }
    a946->InstrTS = now + 1;
    a946->IBus = A946_BusDone;
    // CHECKME: i dont know if this resets the itcm multiplexer?
    a946->ITCMMultiplexData = false;
}

/*
    notes on ITCM: (from arm946e-s reference manual)
    instr + read = 2 cycles (read stalled)
    write -> read = 3 cycles (write addr needs to be input in sync with wr data, so read addr is delayed)
    write -> instr = 3 cycles (write addr needs to be input in syns with wr data, so instr addr is delayed)
    read -> instr = 3 cycles (read stalled ??? multiplexer???????)
    instr + write = 2 cycles (instr first)
*/
void A946_InstrFetchITCM(ARM946ES* a946, timestamp now)
{
    const u32 addr = a946->ARM.PC;

    u32 fetch = MemoryRead(32, a946->ITCM, addr, A946_ITCMSize);
    a946->InstrTS = now + 1 + a946->ITCMMultiplexData;
    a946->InstrLatch = fetch;
}

bool A946_InstrFetchICache(ARM946ES* a946, timestamp now)
{
    const u32 addr = a946->ARM.PC;
    u32 fetch;
    if (A946_ICacheLookup(a946, addr, now, &fetch))
    {
        a946->InstrTS = now + 1;
        a946->InstrLatch = fetch;
        return true;
    }
    else return false;
}

void A946_InstrFetchBIU(ARM946ES* a946, timestamp now)
{
    const u32 addr = a946->ARM.PC;

    a946->BIU.InstrAddr = addr & ~0x3;
    a946->BIU.InstrType = A946BIU_InstrSingle;
    // idk if i need to set the prog trackers if it can't do a burst?
    a946->BIU.InstrCur = 0;
    a946->BIU.InstrMax = 1;
}

void A946_UpdateInstrRegion(ARM946ES* a946)
{
    const u32 addr = a946->ARM.PC;
    const A946_MPUPerms perms = A946_RegionLookup(a946, addr, a946->ARM.Privileged);

    if (!perms.Exec)
    {
        a946->InstrBus = A9InstrBus_Abort;
    }
    else if (A946_ITCMTryRead(a946, addr))
    {
        a946->InstrBus = A9InstrBus_ITCM;
    }
    else if (perms.ICache)
    {
        a946->InstrBus = A9InstrBus_ICache;
    }
    else
    {
        a946->InstrBus = A9InstrBus_BIU;
    }
}

void A946_InstrRead(ARM946ES* a946, timestamp now)
{
    const u32 addr = a946->ARM.PC;

    if (a946->BIU.InstrType == A946BIU_InstrCache)
    {
        // nonsequentials must wait for dcache streaming to complete fully
        if (!a946->ARM.CodeSeq)
        {
            a946->IStreamWait = -1; // 
            // bus busy; wait until available.
        }
        else
        {
            a946->IStreamWait = (addr & 0x1F) / 4;
            // wait for stream to complete.
        }
        return;
    }
    // supposedly arm9 only checks mpu perms on non-sequentials or when crossing 4KiB boundaries (the min region granularity)
    // its supposed to forcibly split bursts on 4KiB boundaries as a result.
    // note: im checking for sequential here
    if (!(addr & (KiB(4)-1)) && a946->ARM.CodeSeq)
    {
        a946->ARM.CodeSeq = false;
        A946_UpdateInstrRegion(a946);
    }

    // fetch from latched instruction
    // NOTE: the technical reference manual describes the instruction latching as part of the BIU (ARM946E-S AHB interface)
    // but it seems to apply to itcm fetches as well, so presumably its done by the ARM9E-S core (or right before its sent to the core?)
    if ((addr & 2) && a946->ARM.CodeSeq)
    {
        if (!a946->ARM.CPSR.Thumb) CrashSpectacularly("a946: Misaligned Instruction Fetch in ARM Mode?\n");
        a946->InstrTS = now + 1; // checkme
    }
    else // new fetch will be performed.
    {
        switch(a946->InstrBus)
        {
        case A9InstrBus_Abort:
            A946_InstrFetchAborted(a946, now);
            return; // this instruction handles setting the fetched instruction data itself

        case A9InstrBus_ITCM:
            A946_InstrFetchITCM(a946, now);
            break;

        case A9InstrBus_ICache:
            if (A946_InstrFetchICache(a946, now))
            {
                break; // cache hit; use fast path.
            }
            else
            {
                return; // cache miss; exit and take slow path through biu
            }

        case A9InstrBus_BIU:
            A946_InstrFetchBIU(a946, now);
            return; // cannot take fast path.
        }
    }
    return A946_InstrRead_Post(a946, addr);
}

void A946_InstrRead_Post(ARM946ES* a946, const u32 addr)
{
    u32 instr = a946->InstrLatch;
    if (a946->ARM.CPSR.Thumb)
    {
        // fetch the correct halfword from the latched instruction
        // big endian mode switches the order each halfword is used.
        u8 rotate = ((!!(addr & 2)) ^ a946->CP15.CR.BigEndian) * 16;
        instr = ROR32(instr, rotate) & 0xFFFF;

        a946->ARM.Instr[2] = (ARM_Instr){.Raw = instr,
                                         .Aborted = false,
                                         .CoprocPriv = a946->ARM.Privileged};
    }
    else
    {
        a946->ARM.Instr[2] = (ARM_Instr){.Raw = instr,
                                         .Aborted = false,
                                         .CoprocPriv = a946->ARM.Privileged};
    }
    a946->IBus = A946_BusDone;
    a946->ITCMMultiplexData = false;
    A946_AddMemCycles(a946);
}

u32 A946_DataRead(ARM946ES* a946, const u32 addr, const AHB_HSIZE size, bool seq, timestamp* ts, bool* dabt)
{
    const A946_MPUPerms perms = A946_RegionLookup(a946, addr, a946->ARM.Privileged);

    // data abort
    if (!perms.Read)
    {
        LogPrint(LOG_ARM9|LOG_EXCEP, "DATA ABORT: READ FROM: %08X\n", addr);
        *ts += 1;
        *dabt = true;
        return 0;
    }

    // if data cache is streaming
    static_assert(false, "handle cache streaming logic here?\n");
    if (a946->BIU.DataType == A946BIU_DataCache)
    {
        // nonsequentials must wait for dcache streaming to complete fully
        if (!seq)
        {
            a946->DStreamWait = 8;
            // bus busy; wait until available.
        }
        else
        {
            a946->DStreamWait = (addr & 0x1F) / 4;
            // wait for stream to complete.
        }
        return 0;
    }

    // ldm/stm (and presumably ldrd/strd too) are forcibly split when crossing 4 KiB boundaries to perform a permission look up again.
    // we aren't actually implementing it that way currently but tbf we could?
    // CHECKME: would it make sense to only do this on ahb accesses? Does it impact abort/itcm/dtcm/cache timings?
    // NOTE: this is probably done somewhere in the arm946e-s memory interface?
    // it doesn't seem to be implemented in the arm9e-s core spec
    if ((addr & (KiB(4)-1)) == 0)
    {
        seq = false;
    }

    // handle write contention
    // CHECKME: does this apply to aborts?
    // CHECKME: This does apply to itcm right?
    // Note: this might be due to address pipelining?
    // TODO: not entirely sure how to model this properly?
    if (*ts < a946->DataWrStall)
        *ts = a946->DataWrStall;

    // priority for data reads: itcm > dtcm > dcache > ahb
    if (A946_ITCMTryRead(a946, addr))
    {
        if (a946->ITCMMultiplexData)
        {
            *ts += 1;
        }
        else
        {
            *ts += 2;
            a946->ITCMMultiplexData = true;
        }
        // TODO: make deferrable...?
        //if (a946->MemTimestamp <= a946->InstrContTS)
        //    a946->MemTimestamp += 1;

        //a946->MemTimestamp += 1;
        //a946->InstrContTS = a946->MemTimestamp;
        return MemoryRead(32, a946->ITCM, addr, A946_ITCMSize);
    }
    else if (A946_DTCMTryRead(a946, addr))
    {
        //a946->MemTimestamp += 1;
        return MemoryRead(32, a946->DTCM, addr, A946_DTCMSize);
    }
    else if (perms.DCache)
    {
        //ret = A946_DCacheReadLookup(a946, addr, timings);
        //return ret;
    }

    if (perms.DCache || perms.Buffer)
    {
        // if bufferable then we need to drain write buffer
        //A946_DrainWriteBuffer(a946, &a946->MemTimestamp);
    }
    else
    {
        // otherwise we simply run the write buffer
        //A946_CatchUpWriteBuffer(a946, &a946->MemTimestamp);
    }

    // note: Technically the initial load in SWP(B) is atomic, but I dont think that actually matters in any way?
    // So I dont think we actually need to handle anything here?
    //ret = A946_AHBRead(a946, &a946->MemTimestamp, addr, size, false, seq);

    //return ret;
}

u32 A9ES_DataRead32(ARM946ES* a9es, u32 addr, bool* seq, bool* dabt)
{
    return A946_DataRead(a9es, addr & ~3, HSIZE_32, seq, dabt);
}

u16 ARES_DataRead16(ARM946ES* a9es, u32 addr, bool* seq, bool* dabt)
{
    u32 ret = A946_DataRead(a9es, addr & ~1, HSIZE_16, seq, dabt);
}

bool A9ES_RotateExtendUnit(u32* val, const u32 addr, const ARM_DataWidth size, const bool signext, const bool bigendian)
{
    switch (size)
    {
    case ARMDataWidth_8:
    {
        u8 rotate = addr & 0x3;
        if (bigendian) rotate ^= 0x3;
        *val = ROR32(*val, (rotate * 8));

        if (signext)
            *val = (s32)(s8)*val;
        else // zero extend
            *val &= 0xFF;

        return true;
    }
    case ARMDataWidth_16:
    {
        // misaligned halfword loads no longer result in odd byte rotate behavior
        u8 rotate = addr & 0x2;
        if (bigendian) rotate ^= 0x2;
        *val = ROR32(*val, (rotate * 8));

        if (signext)
            *val = (s32)(s16)*val;
        else // zero extend
            *val &= 0xFFFF;

        return true;
    }
    case ARMDataWidth_32:
    {
        // for some reason this is architecturally defined behavior?
        // what were they smoking
        // behavior does not change with big endian toggle
        *val = ROR32(*val, ((addr & 0x3) * 8));
        return addr & 0x3;
    }
    default: unreachable();
    }
}

u32 A9ES_DataRead8(ARM946ES* a9es, u32 addr, bool* seq, bool* dabt)
{
    u32 ret = A946_DataRead(a9es, addr, HSIZE_8, seq, dabt);

    return ret;
}

void A946_DataWrite(ARM946ES* a946, u32 addr, const u32 val, const u32 ahbmask, const u32 biumask, const bool atomic, const bool deferrable, bool* seq, bool* dabt)
{
    // ldm/stm (and presumably ldrd/strd too) are forcibly split when crossing 4 KiB boundaries to perform a permission look up again.
    // we aren't actually implementing it that way currently but tbf we could?
    // CHECKME: would it make sense to only do this on ahb accesses? Does it impact abort/itcm/dtcm/cache timings?
    // NOTE: this is probably done somewhere in the arm946e-s memory interface?
    // it doesn't seem to be implemented in the arm9e-s core spec
    if ((addr & (KiB(4)-1)) == 0)
    {
        *seq = false;
    }

    //A946_ProgressCacheStream(&a946->MemTimestamp, &a946->DStream, false);

    // handle contention
    // CHECKME: does this apply to aborts?
    // CHECKME: this does actually apply to writes, right?
    // CHECKME: This does apply to itcm right?
    //if (a946->MemTimestamp < a946->DataContTS)
    //    a946->MemTimestamp = a946->DataContTS;

    const A946_MPUPerms perms;// = ARM9_RegionLookup(a946, addr, a946->ARM.Privileged);
    if (!perms.Write)
    {
        //ARM9_Log(a946);
        LogPrint(LOG_ARM9|LOG_EXCEP, "DATA ABORT: WRITE TO: %08X\n", addr);
       // a946->MemTimestamp += 1;
        *dabt = true;
        *seq = true;
        return;
    }
    else if (A946_ITCMTryWrite(a946, addr))
    {
        // itcm writes (and presumably reads too but those matter less) are reordered after the arm9 instruction bus goes;
        // this doesn't apply to instructions like ldm/stm since those start sequential burst and the instr read can't begin until later on in the instruction
        // (internal buses dont seem to interrupt other internal buses?)
        // this does apply to swp interestingly enough.
        if (deferrable)
        {
            //a946->DeferredType = A9BusDefer_Store;
            //a946->DeferredAddr = addr; // this truncates the addr, but that's fine.
            //a946->DeferredVal = val;
            //a946->DeferredMask = biumask;
            *seq = true;
            return;
        }
        else
        {
            //if (a946->MemTimestamp <= a946->InstrContTS)
            //    a946->MemTimestamp += 1;

            MemoryWrite(32, a946->ITCM, addr, A946_ITCMSize, val, biumask);
            //a946->MemTimestamp += 1;
            *seq = true;
            return;
        }
    }
    else if (A946_DTCMTryWrite(a946, addr))
    {
        MemoryWrite(32, a946->DTCM, addr, A946_DTCMSize, val, biumask);

        //a946->MemTimestamp += 1;
        //a946->DataContTS = a946->MemTimestamp + 1;
        *seq = true;
        return;
    }
    else if (perms.DCache && A946_DCacheWriteLookup(a946, addr, val, biumask, perms.Buffer))
    {
        *seq = true;
        return;
    }

    // atomic flag checked for since swp doesn't use the write buffer.
    if ((perms.DCache || perms.Buffer) && !atomic)
    {
        u32 size = stdc_count_ones(ahbmask);
        if (size == 8)
        {
            size = A946WB_8;
        }
        else if (size == 16)
        {
            size = A946WB_16;
        }
        else if (size == 32)
        {
            size = A946WB_32;
        }
        else CrashSpectacularly("arm9 writebuffer mask error: %08X\n", ahbmask);
        // checkme: how does write buffer work with big endian toggle?
        // how does it work if you toggle it before it finishes writing?
        // how does it work if you toggle it while its writing?

        //a946->DataContTS = a946->MemTimestamp + 1;
        // if bufferable then we need to write to the write buffer
       // ARM9_FillWriteBuffer(a946, &a946->MemTimestamp, addr, A946WB_Addr);
       // ARM9_FillWriteBuffer(a946, &a946->MemTimestamp, val, size);
    }
    else
    {
        // otherwise we need to drain write buffer
      //  ARM9_DrainWriteBuffer(a946, &a946->MemTimestamp);
      //  ARM9_AHBWrite(a946, &a946->MemTimestamp, addr, val, ahbmask, atomic, seq);
    }

    *seq = true;
}

void A9ES_DataWrite32(ARM946ES* a9es, u32 addr, u32 val, const bool atomic, const bool deferrable, bool* seq, bool* dabt)
{
    A946_DataWrite(a9es, addr, val, u32_max, u32_max, atomic, deferrable, seq, dabt);
}

void A9ES_DataWrite16(ARM946ES* a9es, u32 addr, u32 val, bool* seq, bool* dabt)
{
    val &= 0xFFFF;
    val |= (val << 16); // arm9 writes are mirrored to unused data lanes; checkme: internally too?
    u32 ahbmask = ROL32(u16_max, ((addr & 2) * 8)); // ahb ignores big endian toggle
    u32 biumask;
    if (a9es->CP15.CR.BigEndian)
        biumask = ROL32(u16_max, (((addr & 2)^2) * 8));
    else
        biumask = ahbmask;
    A946_DataWrite(a9es, addr, val, ahbmask, biumask, false, true, seq, dabt);
}

void A9ES_DataWrite8(ARM946ES* a9es, u32 addr, u32 val, const bool atomic, bool* seq, bool* dabt)
{
    val &= 0xFF;
    val |= (val << 8) | (val << 16) | (val << 24); // arm9 writes are mirrored to unused data lanes; checkme: internally too?
    u32 ahbmask = ROL32(u8_max, ((addr & 3) * 8)); // ahb ignores big endian toggle
    u32 biumask;
    if (a9es->CP15.CR.BigEndian)
        biumask = ROL32(u8_max, (((addr & 3)^3) * 8));
    else
        biumask = ahbmask;
    A946_DataWrite(a9es, addr, val, ahbmask, biumask, atomic, true, seq, dabt);
}
#if 0
void ARM9_DeferredITCMHandler(ARM946ES* a946)
{
    switch(a946->DeferredType)
    {
    case A9BusDefer_None: return;
    case A9BusDefer_Store:
    {
        // CHECKME: Does this cause data bus contention too?
        MemoryWrite(32, a946->ITCM, a946->DeferredAddr, ARM9_ITCMSize, a946->DeferredVal, a946->DeferredMask);

        //timestamp old = a946->MemTimestamp;
        //if (a946->MemTimestamp <= a946->InstrContTS)
        //    a946->MemTimestamp += 1;

        //a946->MemTimestamp += 1;

        a946->DeferredType = A9BusDefer_None;

        // these probably need to run again
        // jakly why did you make the timing logic so convoluted and unintuitive?
        //ARM9_UpdateInterlocks(a946, a946->MemTimestamp - old);
        //ARM9_FetchCycles(a946, 0);
        return;
    }
    case A9BusDefer_Load:
    {
        return;
    }
    }
}
#endif
