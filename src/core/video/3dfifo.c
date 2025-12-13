#include "3d.h"
#include "../console.h"
#include "../io/dma.h"




void GX_UpdateIRQ(struct Console* sys, const timestamp time)
{
    GX3D* gx = &sys->GX3D;

    printf("gx->Status.Raw %08X\n", gx->Status.Raw);
    switch (gx->Status.FIFOIRQMode)
    {
    case 1:
    {
        if (sys->GX3D.Status.FIFOHalfEmpty)
        {
            printf("irq\n");
            Console_ScheduleHeldIRQs(sys, IRQ_3DFIFO, true, time);
        }
        else 
        {
            printf("unirq\n");
            Console_ClearHeldIRQs(sys, IRQ_3DFIFO, true);
        }
        break;
    }
    case 2:
    {
        if (sys->GX3D.Status.FIFOEmpty)
        {
            printf("irq\n");
            Console_ScheduleHeldIRQs(sys, IRQ_3DFIFO, true, time);
        }
        else
        {
            printf("unirq\n");
            Console_ClearHeldIRQs(sys, IRQ_3DFIFO, true);
        }
        break;
    }
    default: // checkme: mode 3?
            printf("unirq\n");
        Console_ClearHeldIRQs(sys, IRQ_3DFIFO, true);
        break;
    }
}

void GX_RunFIFO(struct Console* sys, const timestamp until);

bool GXFIFO_Fill(struct Console* sys, const u8 cmd, const u32 param)
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

bool GXPipe_Fill(struct Console* sys)
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
        StartDMA9(sys, gx->Timestamp, DMAStart_3DFIFO); // TODO: everything involvind when and how this dma type triggers?
        // checkme:?
    }

    return true;
}

bool GXPipe_Drain(struct Console* sys)
{
    GX3D* gx = &sys->GX3D;
    timestamp* ts = &sys->AHB9.Timestamp;

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

bool GX_FetchParams(struct Console* sys)
{
    GX3D* gx = &sys->GX3D;

    //if (gx->CmdBusy) return false;

    bool suc = GXPipe_Drain(sys);

    // TODO: commands and the fifo are weird when you start mixing cmd ports
    // figure out how hardware actually tracks parameters for commands.
    gx->CmdBusy = true;
    return suc;
}


bool GXFIFO_Unpack(struct Console* sys)
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
                //printf("exit?\n");
                gx->BufferFree = true;
                return true;
            }
        }
    }
    gx->ParamRem = ParamLUT[gx->PackBuffer.CurCmd];
    gx->FreshBuffer = false;

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
    return false;
}

void GX_RunFIFO(struct Console* sys, const timestamp until)
{
    GX3D* gx = &sys->GX3D;

    bool empty;
    bool test;
    empty =  test = !GX_RunCommand(sys, until);
    //printf("1:%i\n", test);
    empty &= test = !GXPipe_Fill(sys);
    //printf("2:%i\n", test);
    empty &= test = GXFIFO_Unpack(sys);
    //printf("3:%i\n4:%i\n", test, empty);

    gx->Status.GXBusy = (gx->FIFOFullness != 0) || (gx->PipeWrPtr != 4) || (until < gx->ExecTS) || gx->CmdReady;

    //printf("%lu\n", until);

    if (empty)
    {
        if (until < gx->ExecTS) Schedule_Event(sys, GX_RunFIFO, Evt_GX, gx->ExecTS);
        else                    Schedule_Event(sys, nullptr, Evt_GX, timestamp_max);
    }
    else Schedule_Event(sys, GX_RunFIFO, Evt_GX, until+1);

    gx->Timestamp = until;
}

void GXFIFO_PackedSubmit(struct Console* sys, const u32 val)
{
    GX3D* gx = &sys->GX3D;
    timestamp* ts = &sys->AHB9.Timestamp;

    // loop until we can submit a new command.
    while (true)
    {
        Scheduler_RunEventManual(sys, *ts, Evt_GX, true);
        //printf("%lu %lu\n", *ts, gx->Timestamp);
        //if (*ts < gx->Timestamp)
        //    *ts = gx->Timestamp;
        //printf("huh %lu %lu\n", sys->Sched.EventTimes[Evt_GX], *ts);

        if (gx->ParamRem > 0) // submit a new parameter if needed.
        {
            if (GXFIFO_Fill(sys, gx->PackBuffer.CurCmd, val))
            {
                gx->ParamRem--;
                if (gx->ParamRem <= 0)
                {
                    gx->PackBuffer.All >>= 8;
                    gx->ParamRem = ParamLUT[gx->PackBuffer.CurCmd];
                }
                Schedule_Event(sys, GX_RunFIFO, Evt_GX, *ts+1);
                //gx->Timestamp = *ts+1;
                return;
            }
        }
        else if (gx->BufferFree) // if the buffer is empty then add a new command.
        {
            //gx->UnpackTS = *ts+1; // checkme: +1?
            gx->PackBuffer.All = val;
            gx->FreshBuffer = true;
            gx->BufferFree = false;

            Schedule_Event(sys, GX_RunFIFO, Evt_GX, *ts+1);
            //gx->Timestamp = *ts+1;
            return;
        }
        //Scheduler_RunEventManual(sys, *ts, Evt_GX, true);
        //if (*ts < gx->Timestamp)
        //    *ts = gx->Timestamp;
        Scheduler_StallToRunEvent(sys, ts, Evt_GX, true);
        CR_Switch(sys->HandleMain);
        //++*ts;
    }
}

void GXFIFO_PortSubmit(struct Console* sys, const u32 addr, const u32 val)
{
    GX3D* gx = &sys->GX3D;
    timestamp* ts = &sys->AHB9.Timestamp;

    // loop until we can submit a new command.
    while (true)
    {
        Scheduler_RunEventManual(sys, *ts, Evt_GX, true);

        if (GXFIFO_Fill(sys, addr/4, val))
        {
            Schedule_Event(sys, GX_RunFIFO, Evt_GX, *ts+1);
            gx->Timestamp = *ts;
            return;
        }
        Scheduler_StallToRunEvent(sys, ts, Evt_GX, true);
        CR_Switch(sys->HandleMain);
    }
}

void GX_IOWrite(struct Console* sys, const u32 addr, const u32 mask, const u32 val)
{
    switch(addr & 0x7FF)
    {
        case 0x400 ... 0x43C:
            printf("subm2 %08X\n", val);
            if (mask != 0xFFFFFFFF) LogPrint(LOG_GX|LOG_UNIMP, "Non 32 bit packed command write?\n");
            GXFIFO_PackedSubmit(sys, val);
            break;

        case 0x440 ... 0x5FC:
            printf("subm %08X %08X\n", addr, val);
            if (mask != 0xFFFFFFFF) LogPrint(LOG_GX|LOG_UNIMP, "Non 32 bit command port write?\n");
            GXFIFO_PortSubmit(sys, addr, val);
            break;

        case 0x600:
        {
            MaskedWrite(sys->GX3D.Status.Raw, val, mask & 0xC0000000);
            printf("write %08X\n", sys->GX3D.Status.Raw);
            GX_UpdateIRQ(sys, sys->AHB9.Timestamp);
            break;
        }

        default:
            LogPrint(LOG_GX|LOG_UNIMP, "UNIMPLEMENTED GX COMMAND WRITE %08X %08X\n", addr, val);
            break;
    }
}

u32 GX_IORead(struct Console* sys, const u32 addr)
{
    switch(addr & 0x7FF)
    {
        case 0x600:
            //printf("stat %08X\n", sys->GX3D.Status.Raw | (sys->GX3D.FIFOFullness << 16));
            return sys->GX3D.Status.Raw | (sys->GX3D.FIFOFullness << 16);

        default:
            LogPrint(LOG_GX|LOG_UNIMP, "UNIMPLEMENTED GX COMMAND READ %08X\n", addr);
            return 0;
    }
}
