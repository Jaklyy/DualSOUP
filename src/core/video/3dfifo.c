#include "3d.h"
#include "core/console.h"
#include "core/io/dma.h"
#include "core/scheduler.h"




void GX_UpdateIRQ(Console* sys, const timestamp time)
{
    GX3D* gx = &sys->GX3D;

    switch (gx->Status.FIFOIRQMode)
    {
    case 1:
    {
        if (sys->GX3D.Status.FIFOHalfEmpty)
        {
            Sched_AddEvent(sys, time, Evt_IRQ9_GXFIFO);
        }
        else 
        {
            LevelIRQ9_Stop(sys, IRQ_3DFIFO);
        }
        break;
    }
    case 2:
    {
        if (sys->GX3D.Status.FIFOEmpty)
        {
            Sched_AddEvent(sys, time, Evt_IRQ9_GXFIFO);
        }
        else
        {
            LevelIRQ9_Stop(sys, IRQ_3DFIFO);
        }
        break;
    }
    default: // checkme: mode 3?
        LevelIRQ9_Stop(sys, IRQ_3DFIFO);
        break;
    }
}

void GX_RunFIFO(Console* sys, const timestamp until);

bool GXFIFO_Fill(Console* sys, const u8 cmd, const u32 param)
{
    GX3D* gx = &sys->GX3D;

    if (gx->FIFOFullness == 256) return false;

    gx->FIFO[gx->FIFOWrPtr] = (GXCmd){param, cmd};

    gx->FIFOWrPtr = (gx->FIFOWrPtr + 1) % 256;


    if (gx->FIFOFullness == 0)
    {
        gx->Status.FIFOEmpty = false;
        GX_UpdateIRQ(sys, gx->Timestamp);
    }
    else if (gx->FIFOFullness == 127)
    {
        gx->Status.FIFOHalfEmpty = false;
        GX_UpdateIRQ(sys, gx->Timestamp);
        //StartDMA9(sys, timestamp_max, DMAStart_3DFIFO); // TODO: everything involvind when and how this dma type triggers?
    }
    gx->FIFOFullness++;

    // busy flag is set once there's actually something in the fifo
    gx->Status.GXBusy = true;
    // CHECKME: other busy flags get set here?
    // test busy flag is set weirdly i think?
    // so stack busy might also be...?
    return true;
}

bool GXPipe_Fill(Console* sys, timestamp now)
{
    GX3D* gx = &sys->GX3D;

    // pipe is full
    if (gx->PipeRdPtr == gx->PipeWrPtr) return false;
    // fifo is empty
    if (gx->FIFOFullness == 0) return false;

    // reset empty flag
    if (gx->PipeWrPtr == 4)
    {
        gx->PipeWrPtr = 0;
        gx->PipeRdPtr = 0;
    }

    gx->Pipe[gx->PipeWrPtr] = gx->FIFO[gx->FIFORdPtr];

    gx->PipeWrPtr = (gx->PipeWrPtr + 1) % 4;
    gx->FIFORdPtr = (gx->FIFORdPtr + 1) % 256;

    gx->FIFOFullness--;
    if (gx->FIFOFullness == 0)
    {
        gx->Status.FIFOEmpty = true;
        GX_UpdateIRQ(sys, gx->Timestamp);
    }
    else if (gx->FIFOFullness == 127)
    {
        gx->Status.FIFOHalfEmpty = true;
        GX_UpdateIRQ(sys, gx->Timestamp);
        StartDMA9(sys, gx->Timestamp, DMAStart_3DFIFO); // TODO: everything involving when and how this dma type triggers?
        // checkme:?
    }

    if (gx->FIFOWait) Sched_AddEvent(sys, now, Evt_IO9);

    return true;
}

bool GXPipe_Drain(Console* sys)
{
    GX3D* gx = &sys->GX3D;
    //timestamp* ts = &sys->AHB9.Timestamp;

    //if (gx->PipeTS >= *ts)

    if (gx->PipeWrPtr == 4) return false; // pipe is empty

    gx->CurCmd = gx->Pipe[gx->PipeRdPtr];

    gx->PipeRdPtr = (gx->PipeRdPtr + 1) % 4;
    // set pipe empty flag
    if (gx->PipeRdPtr == gx->PipeWrPtr)
    {
        gx->PipeWrPtr = 4;
    }

    return true;
}

bool GX_FetchParams(Console* sys)
{
    GX3D* gx = &sys->GX3D;

    //if (gx->CmdBusy) return false;

    bool suc = GXPipe_Drain(sys);

    // TODO: commands and the fifo are weird when you start mixing cmd ports
    // figure out how hardware actually tracks parameters for commands.
    gx->CmdBusy = true;
    return suc;
}


bool GXFIFO_Unpack(Console* sys, timestamp now)
{
    GX3D* gx = &sys->GX3D;

    // search until we find a non-00 command; somehow this takes 0 cycles
    while (gx->PackBuffer.CurCmd == 0)
    {
        gx->PackBuffer.All >>= 8;
        // if there's none we return; *unless* there were no commands period, then a nop is submitted
        if (gx->PackBuffer.All == 0)
        {
            if (gx->FreshBuffer)
            {
                bool suc = GXFIFO_Fill(sys, gx->PackBuffer.CurCmd, 0);
                gx->FreshBuffer = !suc;
                return false;
            }
            else
            {

                if (gx->PackWait) Sched_AddEvent(sys, now, Evt_IO9);
                gx->BufferFree = true;
                return true;
            }
        }
        gx->ParamRem = ParamLUT[gx->PackBuffer.CurCmd];
        gx->FreshBuffer = false;
    }
    if (gx->FreshBuffer)
    {
        gx->ParamRem = ParamLUT[gx->PackBuffer.CurCmd];
        gx->FreshBuffer = false;
    }

    // if cmd has no params we submit it immediately
    if (gx->ParamRem <= 0)
    {
        bool suc = GXFIFO_Fill(sys, gx->PackBuffer.CurCmd, 0);
        if (suc)
        {
            gx->PackBuffer.All >>= 8;
            gx->ParamRem = ParamLUT[gx->PackBuffer.CurCmd];
        }
        return false;
    }
    if (gx->PackWait) Sched_AddEvent(sys, now, Evt_IO9);
    return false;
}

void GX_RunFIFO(Console* sys, const timestamp until)
{
    GX3D* gx = &sys->GX3D;

    bool empty;
    bool test;
    empty =  test = !GX_RunCommand(sys, until);
    empty &= test = !GXPipe_Fill(sys, until);
    empty &= test = GXFIFO_Unpack(sys, until);

    gx->Status.GXBusy = (gx->FIFOFullness != 0) || (gx->PipeWrPtr != 4) || (until < gx->ExecTS) || gx->CmdReady;

    if (empty)
    {
        if (until < gx->ExecTS) Sched_AddEvent(sys, gx->ExecTS, Evt_GX);
    }
    else Sched_AddEvent(sys, until+DSClk33(1), Evt_GX);

    gx->Timestamp = until;
}

bool GXFIFO_PackedSubmit(Console* sys, const u32 val, const timestamp now)
{
    GX3D* gx = &sys->GX3D;

    // loop until we can submit a new command.
    if (gx->ParamRem > 0) // submit a new parameter if needed.
    {
        if (GXFIFO_Fill(sys, gx->PackBuffer.CurCmd, val))
        {
            gx->ParamRem -= 1;
            if (gx->ParamRem <= 0)
            {
                gx->PackBuffer.All >>= 8;
                gx->ParamRem = ParamLUT[gx->PackBuffer.CurCmd];
            }
            Sched_AddEvent(sys, now+DSClk33(1), Evt_GX);
            return true;
        }
    }
    else if (gx->BufferFree) // if the buffer is empty then add a new command.
    {
        gx->PackBuffer.All = val;
        gx->FreshBuffer = true;
        gx->BufferFree = false;

        Sched_AddEvent(sys, now+DSClk33(1), Evt_GX);
        return true;
    }
    return false;
}

bool GXFIFO_PortSubmit(Console* sys, const u32 addr, const u32 val, const timestamp now)
{
    GX3D* gx = &sys->GX3D;

    if (GXFIFO_Fill(sys, addr/4, val))
    {
        Sched_AddEvent(sys, now+DSClk33(1), Evt_GX);
        gx->Timestamp = now;
        return true;
    }
    return false;
}

bool GX_IOWrite(Console* sys, const u32 addr, const u32 mask, const u32 val, const timestamp now)
{
    GX3D* gx = &sys->GX3D;

    switch(addr & 0x7FF)
    {
        case 0x330 ... 0x33C:
            MemoryWrite(32, gx->EdgeTable, addr, sizeof(gx->EdgeTable), val, mask);
            break;

        case 0x340:
            MaskedWrite(gx->AlphaThreshold, val, mask & 0x1F);
            break;

        case 0x350:
            MaskedWrite(gx->RearAttr.Raw, val, mask & 0x3F1FFFFF);
            break;

        case 0x354:
            MaskedWrite(gx->RearDepth, val, mask & 0x7FFF);
            break;

        case 0x358:
            MaskedWrite(gx->FogColor, val, mask & 0x001F7FFF);
            break;

        case 0x35C:
            MaskedWrite(gx->FogOffset, val, mask & 0x7FFF);
            break;

        case 0x360 ... 0x37C:
            MemoryWrite(32, gx->FogTable, addr, sizeof(gx->FogTable), val, mask & 0x7F7F7F7F);
            break;

        case 0x380 ... 0x3BC:
            MemoryWrite(32, gx->ToonTable, addr, sizeof(gx->ToonTable), val, mask & 0x7FFF7FFF);
            break;

        case 0x400 ... 0x43C:
            //printf("subm2 %08X\n", val);
            if (mask != 0xFFFFFFFF) LogPrint(LOG_GX|LOG_UNIMP, "Non 32 bit packed command write?\n");
            //printf("pack %02X %08X\n", gx->PackBuffer.CurCmd, val);
            return GXFIFO_PackedSubmit(sys, val, now);

        case 0x440 ... 0x5FC:
            //printf("subm %08X %08X\n", addr, val);
            if (mask != 0xFFFFFFFF) LogPrint(LOG_GX|LOG_UNIMP, "Non 32 bit command port write?\n");
            //printf("port %02X %08X\n", (addr/4) & 0xFF, val);
            return GXFIFO_PortSubmit(sys, addr, val, now);

        case 0x600:
        {
            MaskedWrite(gx->Status.Raw, val, mask & 0xC0000000);
            if (mask & val & (1<<15))
            {
                gx->Status.StackError = false;
                gx->PosVecMtxStackPtr = 0;
                gx->ProjMtxStackPtr = 0;
                gx->TexMtxStackPtr = 0;
            }
            GX_UpdateIRQ(sys, now);
            break;
        }

        default:
            LogPrint(LOG_GX|LOG_UNIMP, "UNIMPLEMENTED 3D WRITE %08X %08X\n", addr, val);
            break;
    }
    return true;
}

u32 GX_IORead(Console* sys, const u32 addr)
{
    GX3D* gx = &sys->GX3D;

    switch(addr & 0x7FF)
    {
        case 0x600:
            //printf("stat %08X\n", gx->Status.Raw | (gx->FIFOFullness << 16));
            return gx->Status.Raw | (gx->FIFOFullness << 16) | (gx->ProjMtxStackPtr << 13) | ((gx->PosVecMtxStackPtr & 0x1F) << 8);

        case 0x604:
            return gx->PolyRAMPtr | (gx->VtxRAMPtr << 16);

        case 0x620 ... 0x62F:
            return gx->PosTestRes[(addr / 4) % 4];

        case 0x630:
            return gx->VecTestRes[0] | (u32)gx->VecTestRes[1] << 16;
        case 0x634:
            return gx->VecTestRes[2];

        case 0x640 ... 0x67C:
            GX_UpdateClip(sys);
            return gx->ClipMatrix.Arr[(addr & 0x3C)/4];

        case 0x680 ... 0x688:
            return gx->VectorMatrix.Arr[((addr & 0xC)/4)+0];
        case 0x68C ... 0x694:
            return gx->VectorMatrix.Arr[(((addr) & 0xC)/4)+4];
        case 0x698 ... 0x6A0:
            return gx->VectorMatrix.Arr[(((addr) & 0xC)/4)+8];

        default:
            LogPrint(LOG_GX|LOG_UNIMP, "UNIMPLEMENTED 3D READ %08X\n", addr);
            return 0;
    }
}
