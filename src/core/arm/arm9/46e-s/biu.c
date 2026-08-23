#include "core/utils.h"
#include "core/scheduler.h"
#include "../arm.h"





#if 0
void ARM9_RunWriteBuffer(ARM946ES* ARM9)
{
    ARM9_WriteBuffer* buf = &ARM9->WBuffer;

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

void ARM9_CatchUpWriteBuffer(ARM946ES* ARM9, timestamp* until)
{
#ifdef wb
    ARM9_WriteBuffer* buf = &ARM9->WBuffer;

    while((buf->FIFOFillPtr != 16 || buf->Latched) && ((*until > buf->NextStep) || buf->BufferSeq))
    {
        ARM9_RunWriteBuffer(ARM9);
    }
#endif
}

void ARM9_DrainWriteBuffer(ARM946ES* ARM9, timestamp* until)
{
#ifdef wb
    ARM9_WriteBuffer* buf = &ARM9->WBuffer;

    // loop until write buffer is empty
    while(buf->FIFOFillPtr != 16 || buf->Latched)
    {
        ARM9_RunWriteBuffer(ARM9);
    }
    if (*until < buf->NextStep)
        *until = buf->NextStep;
#endif
}

void ARM9_FillWriteBuffer(ARM946ES* ARM9, timestamp* now, u32 val, u8 flag)
{
    ARM9_WriteBuffer* buf = &ARM9->WBuffer;

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
#endif

#if 0
// this function results in the timestamp being aligned with the bus clock
void ARM9_AHBAccess(ARM946ES* ARM9, timestamp* ts, const bool atomic, bool* seq)
{
    // stall until aligned with external bus clock
    // TODO: a lot of this logic breaks once you realize that the arm9 isn't the only component that can change the arm9 clock.
    // I dont know how to fix that........
    *ts = ((*ts + A9ClockRound(*ARM9)) >> A9ClockShift(*ARM9));

    // Note: external bus accesses have a fixed latency due to buffering.
    // this can begin while other internal arm9 buses are using the external bus, thus hiding part or even all of the latency.

    // the arm9 has multiple components capable of accessing the bus at the same time
    // and they are capable of queueing ext. bus accesses while other int. buses are using it.
    // so we should check if arm9 already had ownership of the bus from accesses that were handled earlier.
    if (*ts < ARM9->LastBusTime)
    {
        // CHECKME: does it make sense to wait until the bus was actually attained?
        // it's more correct to do so but im not sure it matters in practice?

        // NOTE: this code path should't trigger with sequential accesses
        //if (*seq) LogPrint(LOG_UNIMP, "Sequential in codepath that sequentials should'nt be...?\n");

        // CHECKME: how exactly does latency and losing bus ownership work with sharing?
        *ts += A9BusLatency(*ARM9);

        // make sure we dont immediately lose bus ownership
        // if we dont lose ownership we don't have to reincur latency
        if (AHB_NegOwnership(ARM9->ARM.Sys, ts, atomic, true))
            return;
        else
            *seq = false;
    }
    else
    {
        // wait until we're able to take ownership of the ARM9 side bus.
        // once we have ownership we can't lose it until the access fully ends.
        // we also can't lose ownership during an atomic access.
        if (!AHB_NegOwnership(ARM9->ARM.Sys, ts, atomic, true))
            *seq = false;
    }

    // add latency. technically still applies for sequential accesses but it's weird tm
    if (!*seq) *ts += A9BusLatency(*ARM9);
}

u32 ARM9_AHBRead(ARM946ES* ARM9, timestamp* ts, const u32 addr, const AHB_HSIZE size, const bool atomic, bool* seq)
{
    // handle external bus logic
    ARM9_AHBAccess(ARM9, ts, false, seq);

    // ahb accesses must not carry a burst across a 1KiB boundary
    if ((addr & (KiB(1)-1)) == 0)
    {
        *seq = false;
    }

    // actually read off of the bus
    u32 ret = AHB9_Read(ARM9->ARM.Sys, ts, addr, size, atomic, false, seq, true);
    ARM9->LastBusTime = *ts;

    // convert clock back
    // the arm9 interacts with the bus on the rising edge of the bus clock so we get the result on the first cycle of the clock.
    // so we do this weird looking thing to get the right effect.
    *ts = ((*ts - 1) << A9ClockShift(*ARM9)) + 1;

    return ret;
}

void ARM9_AHBWrite(ARM946ES* ARM9, timestamp* ts, const u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq)
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
    *ts = ((*ts - 1) << A9ClockShift(*ARM9)) + 1;
}
#endif

void A946_WriteBufferRun(ARM946ES* a946)
{
    A946_WBuffer* wbuf = &a946->BIU.WBuffer;
    const A946_WBufferFIFO entry = wbuf->FIFOEntry[wbuf->FIFODrainPtr];

    wbuf->FIFODrainPtr = (wbuf->FIFODrainPtr + 1) % countof(wbuf->FIFOEntry);

    if (wbuf->FIFODrainPtr == wbuf->FIFOFillPtr)
        wbuf->Empty = true;

    switch(entry.Flags)
    {
    case A946WB_Addr:
        wbuf->Addr = entry.Data;
        break;
    }
}

void A946_WriteBufferFill(ARM946ES* a946, const u32 data, const A946_WBufferFlags flags)
{
    static_assert(false, "handle full writebuffer somehow\n");
    A946_WBuffer* wbuf = &a946->BIU.WBuffer;

    wbuf->FIFOEntry[wbuf->FIFOFillPtr] = (A946_WBufferFIFO){.Data=data, .Flags=flags};

    wbuf->FIFOFillPtr = (wbuf->FIFOFillPtr + 1) % countof(wbuf->FIFOEntry);
    wbuf->Empty = false;
}

void A946_BIURun(ARM946ES* a946, timestamp now)
{
    if (a946->BIU.DataType != A946BIU_DataNone)
    {
        NeoSched_AddEventIfEarlier(a946->ARM.Sys, NTRClock_67Align33(now), Evt_AHB9);
    }
    else if (a946->BIU.InstrType != A946BIU_InstrNone)
    {
        NeoSched_AddEventIfEarlier(a946->ARM.Sys, NTRClock_67Align33(now), Evt_AHB9);
    }
    else if (!a946->BIU.WBuffer.Empty)
    {
        NeoSched_AddEventIfEarlier(a946->ARM.Sys, NTRClock_67Align33(now), Evt_AHB9);
    }
}

void A946_BIUInstr_Post(ARM946ES* a946, u32 addr, u32 val, timestamp now)
{
    static_assert(false, "make sure these timings still work when you figure out wtf you're doing about this shit\n");
    a946->BIU.InstrCur = A946BIU_InstrNone;
    a946->InstrTS = now - 1;
    a946->InstrLatch = val;
    A946_InstrRead_Post(a946, addr);
}

void A946_BIUDataWrapup()
{

}
