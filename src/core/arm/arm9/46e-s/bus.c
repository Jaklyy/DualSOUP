#include "core/bus/bus.h"
#include "core/arm/shared/arm.h"
#ifdef __AVX2__
    #include <immintrin.h>
#endif
#include "core/utils.h"
#include "../arm.h"
#include "core/scheduler.h"




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
    for (s32 i = 7; i >= 0; i--)
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
    a946->InstrTS = now + DSClk67(1);
    A9ES_InstrDone(a946);
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
    a946->InstrTS = now + DSClk67(1 + a946->ITCMMultiplexData);
    a946->InstrLatch = fetch;
}

bool A946_InstrFetchICache(ARM946ES* a946, timestamp now)
{
    const u32 addr = a946->ARM.PC;
    u32 fetch;
    if (A946_ICacheLookup(a946, addr, now, &fetch))
    {
        a946->InstrTS = now + DSClk67(1);
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
    a946->BIU.InstrMax = 1;
    a946->BIU.InstrSubmCur = 0;
    a946->BIU.InstrCompCur = 0;
    A9ES_InstrBusy(a946);
    A946_BIUSched(a946, now);
}

void A946_UpdateInstrRegion(ARM946ES* a946)
{
    const u32 addr = a946->ARM.PC;
    const A946_MPUPerms perms = A946_RegionLookup(a946, addr, a946->ARM.Privileged);

    if (!perms.Exec)
        a946->InstrBus = A9InstrBus_Abort;
    else if (A946_ITCMTryRead(a946, addr))
        a946->InstrBus = A9InstrBus_ITCM;
    else if (perms.ICache)
        a946->InstrBus = A9InstrBus_ICache;
    else
        a946->InstrBus = A9InstrBus_BIU;
}

void A946_InstrRead(ARM946ES* a946, timestamp now)
{
    const u32 addr = a946->ARM.PC;

    // supposedly arm9 only checks mpu perms on non-sequentials or when crossing 4KiB boundaries (the min region granularity)
    // its supposed to forcibly split bursts on 4KiB boundaries as a result.
    // note: im checking for sequential here
    if (!(addr & (KiB(4)-1)) && a946->ARM.CodeSeq)
    {
        a946->ARM.CodeSeq = false;
        A946_UpdateInstrRegion(a946);
    }

    if (a946->BIU.InstrType == A946BIU_InstrCache)
    {
        if (!a946->ARM.CodeSeq) a946->IStreamWaitCur = -1; // nonsequential; wait for cache streaming to complete fully.
        else a946->IStreamWaitCur = ((addr&0x1F)/4)+1; // sequential; wait for addr to be fetched.
        return A9ES_InstrBusy(a946);
    }

    // fetch from latched instruction
    // NOTE: the technical reference manual describes the instruction latching as part of the BIU (ARM946E-S AHB interface)
    // but it seems to apply to itcm fetches as well, so presumably its done by the ARM9E-S core (or right before its sent to the core?)
    if ((addr & 2) && a946->ARM.CodeSeq)
    {
        if (!a946->ARM.CPSR.Thumb) CrashSpectacularly("a946: Misaligned Instruction Fetch in ARM Mode?\n");
        a946->InstrTS = now + DSClk67(1); // checkme
    }
    else // new fetch will be performed.
    {
        switch(a946->InstrBus)
        {
        case A9InstrBus_Abort:
            A946_InstrFetchAborted(a946, now); return; // this instruction handles setting the fetched instruction data itself
        case A9InstrBus_ITCM:
            A946_InstrFetchITCM(a946, now); break;
        case A9InstrBus_ICache:
            if (A946_InstrFetchICache(a946, now)) break; // cache hit; use fast path.
            else return; // cache miss; exit and take slow path through biu
        case A9InstrBus_BIU:
            A946_InstrFetchBIU(a946, now); return; // cannot take fast path.
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
    A9ES_InstrDone(a946);
    a946->ITCMMultiplexData = false;
    Sched_AddEvent(a946->ARM.Sys, a946->InstrTS, Evt_ARM9);
}

#define AddMem(x) (a946->DataTS = now + (DSClk67(1) * numfetch))
void A946_DataRead(ARM946ES* a946, timestamp now)
{
    A9ES_PostMem* pass = &a946->PostMem;

    u32 addr = pass->Addr + (pass->NumFetchCompleted*4);
    u8 numfetch = pass->NumFetch - pass->NumFetchCompleted;

    const A946_MPUPerms perms = A946_RegionLookup(a946, addr, pass->Priv);

    // ldm/stm (and presumably ldrd/strd too) are forcibly split when crossing 4 KiB boundaries to perform a permission look up again.
    // CHECKME: Does it impact abort/itcm/dtcm/cache timings?
    // NOTE: this is probably done somewhere in the arm946e-s memory interface?
    // it doesn't seem to be implemented in the arm9e-s core spec
    if (numfetch > 1) // CHECKME: is this check faster?
    {
        u32 start = addr / 4;
        u32 end = start + (numfetch-1);
        constexpr u32 mpu = (KiB(4)/4);
        if ((end & mpu) != (start & mpu))
            numfetch = (end & ~(mpu-1)) - start;
    }

    // data abort
    if (!perms.Read)
    {
        LogPrint(LOG_ARM9|LOG_EXCEP, "DATA ABORT: READ FROM: %08"PRIX32"\n", addr);
        pass->DataAbort = true;
        AddMem(1);
        pass->NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);
    }

    // if data cache is streaming
    if (a946->BIU.DataType == A946BIU_DataCache)
    {
        // nonsequentials must wait for dcache streaming to complete fully
        a946->DStreamWaitCur = -1;
        return;
    }

    // handle write contention
    // CHECKME: does this apply to aborts?
    // CHECKME: This does apply to itcm right?
    // Note: this might be due to address pipelining?
    // TODO: not entirely sure how to model this properly?
    if (now < a946->DataWrStall)
        now = a946->DataWrStall;

    // priority for data reads: itcm > dtcm > dcache > ahb
    if (A946_ITCMTryRead(a946, addr))
    {
        if (!a946->ITCMMultiplexData)
        {
            now += 1;
            a946->ITCMMultiplexData = true;
        }
        for (u8 i = 0; i < numfetch; i++)
            pass->RData[i+pass->NumFetchCompleted] = MemoryRead(32, a946->ITCM, addr + (i*4), A946_ITCMSize);

        AddMem(1);
        pass->NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);
    }
    else if (A946_DTCMTryRead(a946, addr))
    {
        for (u8 i = 0; i < numfetch; i++)
            pass->RData[i+pass->NumFetchCompleted] = MemoryRead(32, a946->DTCM, addr + (i*4), A946_DTCMSize);

        AddMem(1);
        pass->NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);
    }
    else if (perms.DCache)
    {
        // dcache needs to be split further into cache lines
        // CHECKME: this implementation results in it doing a ns access on the start of the next cache line, this might matter.
        if (numfetch > 1) // CHECKME: is this check faster?
        {
            u32 start = addr / 4;
            u32 end = start + (numfetch-1);
            if ((end & A946_DCacheLineLength) != (start & A946_DCacheLineLength))
                numfetch = (end & ~(A946_DCacheLineLength-1)) - start;
        }

        return A946_DCacheReadLookup(a946, (AHB_HPROT){.Data=true, .Privileged=pass->Priv, .Bufferable=perms.Buffer, .Cacheable=perms.DCache}, addr, now, numfetch);
    }
    else
    {
        const ARM_DataWidth size = pass->Size;

        a946->PostMem.DataPtr = pass->NumFetchCompleted;
        pass->NumFetchCompleted += numfetch;

        a946->BIU.DataAddr = (addr >> size) << size;
        a946->BIU.DataType = (pass->DataCB == A9ESDataCB_SwapLoad) ? A946BIU_DataSwapLoad : A946BIU_DataLoad;
        a946->BIU.DataMax = numfetch;
        a946->BIU.DataSubmCur = 0;
        a946->BIU.DataCompCur = 0;
        a946->BIU.DataProt = (AHB_HPROT){.Data=true, .Privileged=pass->Priv, .Bufferable=perms.Buffer, .Cacheable=perms.DCache};
        a946->BIU.DataWidth = size;
        A946_BIUSched(a946, now);
    }
}

void A946_DataWrite(ARM946ES* a946, timestamp now)
{
    A9ES_PostMem* pass = &a946->PostMem;

    u32 addr = pass->Addr + (pass->NumFetchCompleted*4);
    u8 numfetch = pass->NumFetch - pass->NumFetchCompleted;
    const ARM_DataWidth size = pass->Size;

    // ldm/stm (and presumably ldrd/strd too) are forcibly split when crossing 4 KiB boundaries to perform a permission look up again.
    // CHECKME: Does it impact abort/itcm/dtcm/cache timings?
    // NOTE: this is probably done somewhere in the arm946e-s memory interface?
    // it doesn't seem to be implemented in the arm9e-s core spec
    if (numfetch > 1) // CHECKME: is this check faster?
    {
        u32 start = addr / 4;
        u32 end = start + (numfetch-1);
        constexpr u32 mpu = (KiB(4)/4);
        if ((end & mpu) != (start & mpu))
            numfetch = (end & ~(mpu-1)) - start;
    }
    const A946_MPUPerms perms = A946_RegionLookup(a946, addr, pass->Priv);

    // data abort
    if (!perms.Write)
    {
        LogPrint(LOG_ARM9|LOG_EXCEP, "DATA ABORT: WRITE TO: %08X\n", addr);
        AddMem(1);
        pass->DataAbort = true;
        pass->NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);
    }

    // if data cache is streaming
    if (a946->BIU.DataType == A946BIU_DataCache)
    {
        // nonsequentials must wait for dcache streaming to complete fully
        a946->DStreamWaitCur = -1;
        A9ES_DataBusy(a946);
        return;
    }

    // handle contention
    // CHECKME: does this apply to aborts?
    // CHECKME: this does actually apply to writes, right?
    // CHECKME: This does apply to itcm right?
    //if (a946->MemTimestamp < a946->DataContTS)
    //    a946->MemTimestamp = a946->DataContTS;

    // compute byte lanes used for internal accesses
    u32 intaddr = addr;
    if (a946->CP15.CR.BigEndian) intaddr = (addr ^ 0x3); // NOTE: internal writes support big endian properly!
    u32 wrlanes = MakeWriteMask(intaddr >> size << size, size);

    if (A946_ITCMTryWrite(a946, addr))
    {
        if (!a946->ITCMMultiplexData)
        {
            now += DSClk67(1);
            a946->ITCMMultiplexData = true;
        }
        for (u8 i = 0; i < numfetch; i++)
            MemoryWrite(32, a946->ITCM, addr+(i*4), A946_ITCMSize, pass->WrData[i+pass->NumFetchCompleted], wrlanes);

        AddMem(1);
        a946->DataWrStall = a946->DataTS+DSClk67(1);
        pass->NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);
        return;
    }
    else if (A946_DTCMTryWrite(a946, addr))
    {
        for (u8 i = 0; i < numfetch; i++)
            MemoryWrite(32, a946->DTCM, addr+(i*4), A946_DTCMSize, pass->WrData[i+pass->NumFetchCompleted], wrlanes);

        AddMem(1);
        a946->DataWrStall = a946->DataTS+DSClk67(1);
        pass->NumFetchCompleted += numfetch;
        A9ES_DataDone(a946);
        return;
    }
    else if (perms.DCache)
    {
        // dcache needs to be split further into cache lines
        // CHECKME: this implementation results in it doing a ns access on the start of the next cache line, this might matter.
        if (numfetch > 1) // CHECKME: is this check faster?
        {
            u32 start = addr / 4;
            u32 end = start + (numfetch-1);
            if ((end & A946_DCacheLineLength) != (start & A946_DCacheLineLength))
                numfetch = (end & ~(A946_DCacheLineLength-1)) - start;
        }
        if (A946_DCacheWriteLookup(a946, addr, now, wrlanes, numfetch, perms.Buffer))
            return;
    }

    // swp doesn't use the write buffer.
    if ((perms.DCache || perms.Buffer) && (pass->DataCB != A9ESDataCB_SwapStore))
    {
        // checkme: how does write buffer work with big endian toggle?
        // how does it work if you toggle it before it begins writing?
        // how does it work if you toggle it while its writing?
        A946_WriteBufferFill(a946, now, &pass->WrData[pass->NumFetchCompleted], addr, size, numfetch, A946WBCause_DataDir);
        pass->NumFetchCompleted += numfetch;
        A9ES_DataBusy(a946);
    }
    else
    {
        const ARM_DataWidth size = pass->Size;

        for (u8 i = 0; i < numfetch; i++)
            a946->BIU.WriteVal[i] = pass->WrData[i+pass->NumFetchCompleted];

        pass->NumFetchCompleted += numfetch;

        a946->BIU.DataAddr = (addr >> size) << size;
        a946->BIU.DataType = (pass->DataCB == A9ESDataCB_SwapStore) ? A946BIU_DataSwapStore : A946BIU_DataStore;
        a946->BIU.DataMax = numfetch;
        a946->BIU.DataSubmCur = 0;
        a946->BIU.DataCompCur = 0;
        a946->BIU.DataProt = (AHB_HPROT){.Data=true, .Privileged=pass->Priv, .Bufferable=perms.Buffer, .Cacheable=perms.DCache};
        a946->BIU.DataWidth = size;
        A946_BIUSched(a946, now);
    }
}

void A9ES_MemCallbacks(ARM946ES* a9es)
{
    switch(a9es->PostMem.DataCB)
    {
    case A9ESDataCB_LoadSingle: A9ES_LDR_Post(a9es); break;
    case A9ESDataCB_LoadMultiple: A9ES_LDM_Post(a9es); break;
    case A9ESDataCB_StoreSingle: A9ES_STR_Post(a9es); break;
    case A9ESDataCB_StoreMultiple: A9ES_STM_Post(a9es); break;
    case A9ESDataCB_SwapLoad: A9ES_SWPLoad_Post(a9es); break;
    case A9ESDataCB_SwapStore: A9ES_SWPStore_Post(a9es); break;
    }
}

void A9ES_RotateExtendUnit(u32* val, const u32 addr, const ARM_DataWidth size, const bool signext, const bool bigendian)
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
        break;
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
        break;
    }
    case ARMDataWidth_32:
    {
        // for some reason this is architecturally defined behavior?
        // what were they smoking
        // behavior does not change with big endian toggle
        *val = ROR32(*val, ((addr & 0x3) * 8));
        break;
    }
    default: unreachable();
    }
}
