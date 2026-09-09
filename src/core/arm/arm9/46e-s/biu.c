#include "core/utils.h"
#include "core/scheduler.h"
#include "core/bus/bus.h"
#include "../arm.h"




void A946_WriteBufferRun(ARM946ES* a946, const timestamp now)
{
    A946_WBuffer* wbuf = &a946->BIU.WBuffer;
    const A946_WBufferFIFO entry = wbuf->FIFOEntry[wbuf->FIFODrainPtr];

    wbuf->FIFODrainPtr = (wbuf->FIFODrainPtr + 1) % countof(wbuf->FIFOEntry);

    if (wbuf->FIFODrainPtr == wbuf->FIFOFillPtr) wbuf->Empty = true;
    wbuf->Full = false;

    switch(entry.Flags)
    {
    case A946WB_Addr:
        wbuf->Addr = entry.Data;
        wbuf->Seq = false;

        if (!wbuf->Empty) // reschedule biu
            Sched_AddEvent(a946->ARM.Sys, now+DSClk67(1), Evt_ARM9BIU);
        break;
    default:
        BusReq req = {
            .Addr = wbuf->Addr,
            .WrVal = entry.Data,
            .Write = true,
            .Lock = false, // swp doesn't use write buffer
            .Man9 = MAN9_ARM9,
            .Prot = {
                .Data = true,
                // CHECKME: im assuming these fields are unused
                //.Privileged = , .Bufferable = true, .Cacheable = ,
            },
            .Size = (AHB_HSIZE)entry.Flags,
            .Type = (wbuf->Seq ? HTRANS_SEQ : HTRANS_NONSEQ),
            .CB = CB9_BIU9WriteBuffer,
        };
        wbuf->Seq = true;
        Bus_Req(a946->ARM.Sys, &req, DSClkAlign33(now), true);

        // TODO: improve hacky burst logic; hw doesn't split bursts if it can help it.
        if (wbuf->Empty || (wbuf->FIFOEntry[wbuf->FIFODrainPtr].Flags == A946WB_Addr))
        {
            if (a946->BIU.InstrFlushWriteBuffer)
            {
                a946->BIU.InstrFlushWriteBuffer = false;
                A9ES_InstrGo(a946, false);
                Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
            }
            a946->BIU.BurstCur = A946BIUBurst_None;
        }
        else a946->BIU.BurstCur = A946BIUBurst_Buffer;
        break;
    }

    if ((wbuf->BufferInsMax != 0) && !wbuf->Full) // try adding more junk into the write buffer
        Sched_AddEvent(a946->ARM.Sys, now+DSClk67(1), Evt_ARM9WBFill);
}

void A946_WriteBufferFill(ARM946ES* a946, const timestamp now, u32* datastart, const u32 addr, const ARM_DataWidth size, const u8 words, const A946_WBCause cause)
{
    A946_WBuffer* wbuf = &a946->BIU.WBuffer;
    a946->BIU.WBFill = cause;

    wbuf->FIFOWaitList[0] = (A946_WBufferFIFO){addr, A946WB_Addr};
    for (u8 i = 0; i < words+1; i++)
        wbuf->FIFOWaitList[i+1] = (A946_WBufferFIFO){datastart[i], (A946_WBufferFlags)size};

    wbuf->BufferInsCur = 0;
    wbuf->BufferInsMax = words+1;

    if (!wbuf->Full) Sched_AddEvent(a946->ARM.Sys, now+DSClk67(1), Evt_ARM9WBFill);
}

void A946_WriteBufferFillRun(ARM946ES* a946, const timestamp now)
{
    A946_WBuffer* wbuf = &a946->BIU.WBuffer;

    wbuf->FIFOEntry[wbuf->FIFOFillPtr] = wbuf->FIFOWaitList[wbuf->BufferInsCur++];

    wbuf->FIFOFillPtr = (wbuf->FIFOFillPtr + 1) % countof(wbuf->FIFOEntry);
    if (wbuf->FIFODrainPtr == wbuf->FIFOFillPtr) wbuf->Full = true;
    wbuf->Empty = false;

    if (wbuf->BufferInsCur == wbuf->BufferInsMax) // done filling
    {
        if (a946->BIU.WBFill == A946WBCause_DCache && (a946->DStreamWaitCur == 0))
        {
            a946->DataTS = now;
            if (a946->BIU.wbfillstupidcont) A9ES_DataGo(a946, &a946->PostMem);
            else A9ES_DataDone(a946);
            Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
        }
        else if (a946->BIU.WBFill == A946WBCause_DataDir)
        {
            a946->DataTS = now;
            a946->DataWrStall = now + DSClk67(1);
            A9ES_DataDone(a946);
            Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
        }
        else if (a946->BIU.WBFill == A946WBCause_CP15)
        {
            A9ES_InstrGo(a946, false);
            Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9);
        }

        wbuf->BufferInsCur = 0;
        wbuf->BufferInsMax = 0;
    }
    else if (!wbuf->Full) // reschedule this
        Sched_AddEvent(a946->ARM.Sys, now+DSClk67(1), Evt_ARM9WBFill);

    // run write buffer
    A946_BIUSched(a946, now + DSClk67(1));
}

typedef enum : u8
{
    DS_BIU946_INT_WE_DID_IT,
    DS_BIU946_INT_DO_WB,
    DS_BIU946_INT_DO_INSTR,
} DSINT_BIURET;

DSINT_BIURET A946_BIUData(ARM946ES* a946, timestamp now)
{
    A946_BIU* biu = &a946->BIU;
    biu->BurstCur = A946BIUBurst_Data;

    BusReq req;
    switch(biu->DataType)
    {
    case A946BIU_DataLoad:
    case A946BIU_DataCache:
        if ((biu->DataProt.Bufferable || biu->DataProt.Cacheable) && (biu->DataSubmCur == 0) && !biu->WBuffer.Empty)
            return DS_BIU946_INT_DO_WB; // drain writebuffer (checkme: dcache behavior?)

        req = (BusReq){
            .Addr = biu->DataAddr,
            .WrVal = 0,
            .Write = false,
            .Lock = false,
            .Man9 = MAN9_ARM9,
            .Prot = biu->DataProt,
            .Size = (AHB_HSIZE)biu->DataWidth,
            .Type = ((biu->DataSubmCur == 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
            .CB = (A946BIU_DataLoad ? CB9_BIU9DataNormal : CB9_BIU9DataStream),
        };
        biu->DataSubmCur++;
        biu->DataAddr += 4;
        if (biu->DataSubmCur == biu->DataMax) // stop burst
        {
            biu->DataType = A946BIU_DataNone;
            biu->BurstCur = A946BIUBurst_None;
        }
        break;

    case A946BIU_DataStore:
        if (!biu->WBuffer.Empty) // drain writebuffer
            return DS_BIU946_INT_DO_WB;

        req = (BusReq){
            .Addr = biu->DataAddr,
            .WrVal = biu->WriteVal[biu->DataSubmCur],
            .Write = true,
            .Lock = false,
            .Man9 = MAN9_ARM9,
            .Prot = biu->DataProt,
            .Size = (AHB_HSIZE)biu->DataWidth,
            .Type = ((biu->DataSubmCur == 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
            .CB = CB9_BIU9DataNormal,
        };
        biu->DataSubmCur++;
        biu->DataAddr += 4;
        if (biu->DataSubmCur == biu->DataMax) // stop burst
        {
            biu->DataType = A946BIU_DataNone;
            biu->BurstCur = A946BIUBurst_None;
        }
        break;

    case A946BIU_DataSwapLoad:
        if ((biu->DataProt.Bufferable || biu->DataProt.Cacheable) && (biu->DataSubmCur == 0) && !biu->WBuffer.Empty)
            return DS_BIU946_INT_DO_WB; // CHECKME: does this actually drain writebuffer?

        req = (BusReq){
            .Addr = biu->DataAddr,
            .WrVal = 0,
            .Write = false,
            .Lock = true,
            .Man9 = MAN9_ARM9,
            .Prot = biu->DataProt,
            .Size = (AHB_HSIZE)biu->DataWidth,
            .Type = ((biu->DataSubmCur == 0) ? HTRANS_NONSEQ : HTRANS_IDLE /* busy? */),
            .CB = CB9_BIU9DataNormal,
        };
        biu->DataSubmCur++;
        break;

    case A946BIU_DataSwapStore:
        // CHECKME: this really shouldn't drain writebuffer
        req = (BusReq){
            .Addr = biu->DataAddr,
            .WrVal = biu->WriteVal[biu->DataSubmCur],
            .Write = true,
            .Lock = true,
            .Man9 = MAN9_ARM9,
            .Prot = biu->DataProt,
            .Size = (AHB_HSIZE)biu->DataWidth,
            .Type = HTRANS_NONSEQ,
            .CB = CB9_BIU9DataNormal,
        };
        biu->DataType = A946BIU_DataSwapIdle;
        break;

    case A946BIU_DataSwapIdle:
        biu->DataType = A946BIU_DataNone;

        if (biu->InstrType == A946BIU_InstrCache) // icache streaming can hijack the lock for some reason
            return DS_BIU946_INT_DO_INSTR;

        // note: actual contents of address and wrdata bus unknown
        // but i (foolishly?) assume the nds doesn't use them
        req = (BusReq){
            .Addr = 0,
            .WrVal = 0,
            .Write = false,
            .Lock = false, // signal that the next access unlocks the bus (NOTE: hw implements this access as locked, and the next will signal to unlock the bus; this is significantly simpler to handle though)
            .Man9 = MAN9_ARM9,
            .Prot = biu->DataProt,
            .Size = (AHB_HSIZE)biu->DataWidth,
            .Type = HTRANS_IDLE,
            .CB = CB9_BIU9Idle,
        };
        biu->BurstCur = A946BIUBurst_None;
        break;
        default: unreachable();
    }

    Bus_Req(a946->ARM.Sys, &req, DSClkAlign33(now), true);
    return DS_BIU946_INT_WE_DID_IT;
}

void A946_BIUSched(ARM946ES* a946, const timestamp now)
{
    A946_BIU* biu = &a946->BIU;
    if (!biu->BIUBusy)
        Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9BIU);
}

void A946_BIURun(ARM946ES* a946, timestamp now)
{
    A946_BIU* biu = &a946->BIU;
    switch(biu->BurstCur)
    {
    case A946BIUBurst_None: break;
    case A946BIUBurst_Buffer: goto wbuf;
    case A946BIUBurst_Data: goto data;
    case A946BIUBurst_Instr: goto instr;
    }

    biu->BIUBusy = true;
    if (biu->DataType != A946BIU_DataNone)
    { 
        data:
        switch (A946_BIUData(a946, now))
        {
        case DS_BIU946_INT_DO_INSTR: goto instr;
        case DS_BIU946_INT_DO_WB: goto wbuf;
        case DS_BIU946_INT_WE_DID_IT: break; // we did it!
        }
    }
    else if (biu->InstrType != A946BIU_InstrNone)
    {
        instr:

        biu->BurstCur = A946BIUBurst_Instr;
        BusReq req = {
            .Addr = biu->InstrAddr,
            .WrVal = 0,
            .Write = false,
            .Lock = false,
            .Man9 = MAN9_ARM9,
            .Prot = {
                .Data = false,
                // CHECKME: im assuming these fields are unused by the bus
                .Privileged = false,
                .Bufferable = false,
                .Cacheable = false,
            },
            .Size = HSIZE_32,
            .Type = ((biu->InstrSubmCur == 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
            .CB = (A946BIU_InstrSingle ? CB9_BIU9InstrNormal : CB9_BIU9InstrStream),
        };
        biu->InstrSubmCur++;
        biu->InstrAddr += 4;
        if (biu->InstrSubmCur == biu->InstrMax) // stop burst
        {
            biu->InstrType = A946BIU_InstrNone;
            biu->BurstCur = A946BIUBurst_None;
        }
        Bus_Req(a946->ARM.Sys, &req, DSClkAlign33(now), true);
    }
    else if (!biu->WBuffer.Empty)
    {
        wbuf:
        A946_WriteBufferRun(a946, now);
    }
}

void A946_BIUSubmPost(ARM946ES* a946, timestamp now)
{
    A946_BIU* biu = &a946->BIU;
    if ((biu->DataType != A946BIU_DataNone) || (biu->InstrType != A946BIU_InstrNone) || (!biu->WBuffer.Empty))
        Sched_AddEvent(a946->ARM.Sys, now, Evt_ARM9BIU);
}

void A946_BIUInstrPost(ARM946ES* a946, u32 addr, u32 rdata, timestamp now)
{
    a946->InstrTS = now;
    a946->InstrLatch = rdata;
    A946_InstrRead_Post(a946, addr);
}

void A946_BIUDataPost(ARM946ES* a946, u32 rdata, timestamp now)
{
    A946_BIU* biu = &a946->BIU;
    A9ES_PostMem* pass = &a946->PostMem;
    biu->DataCompCur++;

    if ((pass->DataCB == A9ESDataCB_LoadSingle) || (pass->DataCB == A9ESDataCB_LoadMultiple) || (pass->DataCB == A9ESDataCB_SwapLoad))
    {
        pass->RData[pass->DataPtr++] = rdata;
    }

    if (biu->DataCompCur == biu->DataMax)
    {
        a946->DataTS = now;

        if ((pass->DataCB == A9ESDataCB_StoreSingle) || (pass->DataCB == A9ESDataCB_StoreMultiple) || (pass->DataCB == A9ESDataCB_SwapStore))
            a946->DataWrStall = a946->DataTS+DSClk67(1);

        A9ES_DataDone(a946);
    }
}

void A946_BIUCompPost(ARM946ES* a946, timestamp now, u32 rdata, const BusCallbacks cb)
{
    switch (cb)
    {
    case CB9_BIU9InstrNormal: A946_BIUInstrPost(a946, a946->ARM.PC, rdata, now); break;
    case CB9_BIU9InstrStream: A946_ICacheStream_Post(a946, rdata, now); break;
    case CB9_BIU9WriteBuffer: break;
    case CB9_BIU9DataNormal: A946_BIUDataPost(a946, rdata, now); break;
    case CB9_BIU9DataStream: A946_DCacheStream_Post(a946, rdata, now); break;
    case CB9_BIU9Idle: break;
    default: unreachable();
    }
}

void A946_BIUDataWrapup()
{

}
