#include <string.h>
#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"




bool AHB9_SyncWith7GTE(struct Console* sys);
bool AHB9_SyncWith7GT(struct Console* sys);
bool AHB7_SyncWith9GTE(struct Console* sys);
bool AHB7_SyncWith9GT(struct Console* sys);

// TODO: Regs im 99% confident about:
// Timers

u32 IPC_FIFORead(struct Console* sys, const u32 mask, const bool a9)
{
    struct IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    struct IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);
    timestamp ts = ((a9) ? sys->AHB9.Timestamp : sys->AHB7.Timestamp);

    u32 ret;
    // CHECKME: do both sides need to be enabled for it to work?
    // CHECKME: halfword/byte accesses?
    if (recv->CR.EnableFIFOs)
    {
        // return last value.
        if (recv->CR.RecvFIFOEmpty)
        {
            recv->CR.Error = true;
            return recv->FIFO[(recv->DrainPtr-1) % 16];
        }

        ret = recv->FIFO[recv->DrainPtr];

        // no longer full
        if (recv->CR.RecvFIFOFull)
        {
            recv->CR.RecvFIFOFull = false;
            send->CR.SendFIFOFull = false;
        }

        recv->DrainPtr = (recv->DrainPtr + 1) % 16;

        // now empty
        if (recv->FillPtr == recv->DrainPtr)
        {
            recv->CR.RecvFIFOEmpty = true;
            send->CR.SendFIFOEmpty = true;
            // send irq
            if (send->CR.SendFIFOEmptyIRQ)
            {
                Console_ScheduleIRQs(sys, IRQ_IPCFIFOEmpty, !a9, ts);
            }
        }
    }
    else
    {
        // return oldest value.
        ret = recv->FIFO[recv->DrainPtr];
    }
    return ret;
}

void IPC_FIFOWrite(struct Console* sys, const u32 val, const u32 mask, const bool a9)
{
    struct IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    struct IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);
    timestamp ts = ((a9) ? sys->AHB9.Timestamp : sys->AHB7.Timestamp);

    // CHECKME: do both sides need to be enabled for it to work?
    // CHECKME: halfword/byte accesses?
    if (recv->CR.EnableFIFOs)
    {
        // CHECKME: does this write just get ignored or overwrite?
        if (recv->CR.SendFIFOFull)
        {
            recv->CR.Error = true;
            return;
        }

        MaskedWrite(send->FIFO[send->FillPtr], val, mask);

        // no longer empty
        if (recv->CR.SendFIFOEmpty)
        {
            send->CR.RecvFIFOEmpty = false;
            recv->CR.SendFIFOEmpty = false;
            // send irq
            if (send->CR.RecvFIFONotEmptyIRQ)
            {
                Console_ScheduleIRQs(sys, IRQ_IPCFIFONotEmpty, !a9, ts);
            }
        }

        send->FillPtr = (send->FillPtr + 1) % 16;

        // now full
        if (send->FillPtr == send->DrainPtr)
        {
            send->CR.RecvFIFOFull = true;
            recv->CR.SendFIFOFull = true;
        }
    }
}

void IPC_FIFOCRWrite(struct Console* sys, const u32 val, const u32 mask, bool a9)
{
    struct IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    struct IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);
    timestamp ts = ((a9) ? sys->AHB9.Timestamp : sys->AHB7.Timestamp);

    if (a9) AHB9_SyncWith7GT(sys);
    else    AHB7_SyncWith9GT(sys);

    MaskedWrite(recv->CR.Raw, val, mask & 0x8404);

    // FIFO Flush; CHECKME: does this need power?
    if (mask & val & 1<<3)
    {
        memset(send->FIFO, 0, sizeof(sys->IPCFIFO7));
        recv->CR.SendFIFOEmpty = true;
        send->CR.RecvFIFOEmpty = true;
        recv->CR.SendFIFOFull = false;
        send->CR.RecvFIFOFull = false;
        send->FillPtr = 0;
        send->DrainPtr = 0;
        // send irq
        if (send->CR.SendFIFOEmptyIRQ)
        {
            Console_ScheduleIRQs(sys, IRQ_IPCFIFOEmpty, !a9, ts);
        }
    }

    // ack error
    if (mask & val & (1<<14))
    {
        printf("ack %08X %08X!\n", val, recv->CR.Raw);
        recv->CR.Error = false;
    }
}

void IO9_FinishDiv(struct Console* sys, timestamp now)
{
    s64 num;
    s64 den;
    if (sys->DivCR.DivMode & 1) // 64/32
    {
        num = sys->DivNum.b64;
        den = sys->DivDen.b32[0];
    }
    else if (sys->DivCR.DivMode & 2) // 64/64
    {
        num = sys->DivNum.b64;
        den = sys->DivDen.b64;
    }
    else // 32/32
    {
        num = sys->DivNum.b32[0];
        den = sys->DivDen.b32[0];
    }

    if (den == 0)
    {
        if (sys->DivCR.DivMode == 0)
        {
            sys->DivQuo.b64 = ((num<0) ? 1 : -1) ^ 0xFFFFFFFF00000000;
        }
        else
        {
            sys->DivQuo.b64 = ((num<0) ? 1 : -1);
        }
        sys->DivRem.b64 = num;
    }
    else
    {
        // TIL: division can overflow and dividers really dont like it.
        if ((sys->DivCR.DivMode == 0) && ((s32)num == (s32)-0x80000000) && (den == -1))
        {
            sys->DivQuo.b64 = 0x80000000; // idk why
            sys->DivRem.b64 = 0;
        }
        else if ((num == (s64)-0x8000000000000000) && (den == -1))
        {
            sys->DivQuo.b64 = 0x8000000000000000;
            sys->DivRem.b64 = 0;
        }
        else
        {
            sys->DivQuo.b64 = (num / den);
            sys->DivRem.b64 = (num % den);
        }
    }
    // CHECKME: test divide by 0 timings. (when is flag set? is it full length?)
    sys->DivCR.DivByZero = !sys->DivDen.b64;
    sys->DivCR.Busy = false;

    printf("quo: %lX rem: %lX\n", sys->DivQuo.b64, sys->DivRem.b64);
    Schedule_Event(sys, IO9_FinishDiv, Sched_Divider, timestamp_max);
}

void IO9_StartDiv(struct Console* sys)
{
    sys->DivQuo.b64 = 0;
    sys->DivRem.b64 = 0;
    Schedule_Event(sys, IO9_FinishDiv, Sched_Divider, sys->AHB9.Timestamp + ((sys->DivCR.DivMode == 0 ) ? 18 : 34));
    sys->DivCR.Busy = true;
}


u32 IO7_Read(struct Console* sys, const u32 addr, const u32 mask)
{
    switch(addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_Scanline, false);
            return (sys->VCount << 16);

        case 0x00'00'B0 ... 0x00'00'E0-1:
            return DMA_IOReadHandler(sys->DMA7.Channels, addr);

        case 0x00'01'00 ... 0x00'01'0C:
            return Timer_IOReadHandler(sys->Timers7, sys->AHB7.Timestamp, addr);

        case 0x00'01'30:
            return Input_PollMain(sys->Pad);

        case 0x00'01'34:
            return Input_PollMain(sys->Pad) << 16;

        case 0x00'01'80: // ipcsync
            AHB7_SyncWith9GT(sys);
            return sys->IPCSyncDataTo7
                    | (sys->IPCSyncDataTo9 << 8)
                    | (sys->IPCSyncIRQEnableTo7 << 14);
        case 0x00'01'84:
            AHB7_SyncWith9GT(sys);
            return sys->IPCFIFO7.CR.Raw;

        case 0x00'02'04: // External Memory Control
            return sys->ExtMemCR_Shared.Raw | sys->ExtMemCR_7.Raw;

        case 0x00'02'08: // IME
            return sys->IME7;
        case 0x00'02'10: // IE
            return sys->IE7;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_IF7Update, false);
            return sys->IF7;

        case 0x00'02'40: // VRAM/WRAM Status
            return ((sys->VRAMCR[2].Raw & 0x87) == 0x82) | (((sys->VRAMCR[3].Raw & 0x87) == 0x82) << 1)
                   | sys->WRAMCR << 8;

        case 0x10'00'00:
            return IPC_FIFORead(sys, mask, false);

        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO7 READ: %08lX %08lX @ %08lX\n", addr, mask, sys->ARM7.ARM.PC);
            return 0;
    }
}

void IO7_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    switch(addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_Scanline, false);
            //sys->DispStatRW.Raw = val & mask & 0xB8;

            // TODO VCount Match Settings.

            // CHECKME: byte writes probably behave weirdly.
            if ((mask == 0xFF000000) || (mask == 0x00FF0000)) LogPrint(LOG_UNIMP, "UNTESTED: VCOUNT BYTE WRITES!\n");
            if (mask & 0xFFFF0000)
            {
                sys->VCountUpdate = true;
                // CHECKME: how does this mask out?
                // CHECKME: what about > 262 vcounts?
                sys->VCountNew = ((val & mask) >> 16) & 0x1F;
            }
            break;

        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA7_IOWriteHandler(sys, sys->DMA7.Channels, addr, val, mask);
            break;

        case 0x00'01'00 ... 0x00'01'0C:
            Timer_IOWriteHandler(sys->Timers7, sys->AHB7.Timestamp, addr, val, mask);
            break;

        case 0x00'01'80: // ipcsync
        {
            AHB7_SyncWith9GT(sys);
            if (mask & 0xF00)
            {
                sys->IPCSyncDataTo9 = (val >> 8) & 0xF;
            }

            if ((val & mask & (1<<13)) && sys->IPCSyncIRQEnableTo9)
            {
                Console_ScheduleIRQs(sys, IRQ_IPCSync, true, sys->AHB7.Timestamp);
            }

            if (mask & (1<<14))
            {
                sys->IPCSyncIRQEnableTo7 = val & (1<<14);
            }
            break;
        }

        case 0x00'01'84:
            IPC_FIFOCRWrite(sys, val, mask, false);
            break;
        case 0x00'01'88:
            IPC_FIFOWrite(sys, val, mask, false);
            break;

        case 0x00'02'04: // exmemcnt
            MaskedWrite(sys->ExtMemCR_7.Raw, val, mask & 0x7F);
            break;

        case 0x00'02'08: // IME
            MaskedWrite(sys->IME7, val, mask & 1);
            break;
        case 0x00'02'10: // IE
            MaskedWrite(sys->IE7, val, mask & 0x01DF3FFF);
            break;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_IF7Update, false);
            sys->IF7 &= ~(val & mask);
            break;

        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO7 WRITE: %08lX %08lX %08lX @ %08lX\n", addr, val, mask, sys->ARM7.ARM.PC);
            break;
    }
}

u32 IO9_Read(struct Console* sys, const u32 addr, const u32 mask)
{
    switch (addr & 0xFF'FF'FC)
    {

        case 0x00'00'00:
            return sys->PPU_A.DisplayCR.Raw;

        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_Scanline, true);
            return (sys->VCount << 16) | sys->DispStatRO.Raw |  sys->DispStatRW.Raw;

        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            return DMA_IOReadHandler(sys->DMA9.Channels, addr);
        case 0x00'00'E0 ... 0x00'00'EC:
            return sys->DMAFill[(addr & 0xF) / 4];

        case 0x00'01'00 ... 0x00'01'0C:
            return Timer_IOReadHandler(sys->Timers9, sys->AHB9.Timestamp, addr);

        case 0x00'01'30:
            return Input_PollMain(sys->Pad);

        // IPC
        case 0x00'01'80: // ipcsync
            AHB9_SyncWith7GT(sys);
            return sys->IPCSyncDataTo9
                    | (sys->IPCSyncDataTo7 << 8)
                    | (sys->IPCSyncIRQEnableTo9 << 14);
        case 0x00'01'84:
            AHB9_SyncWith7GT(sys);
        printf("read! %08X!\n", sys->IPCFIFO9.CR.Raw);
            return sys->IPCFIFO9.CR.Raw;

        case 0x00'02'04: // External Memory Control
            return sys->ExtMemCR_Shared.Raw | sys->ExtMemCR_9.Raw;

        case 0x00'02'08: // IME
            return sys->IME9;
        case 0x00'02'10: // IE
            return sys->IE9;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_IF9Update, true);
            return sys->IF9;

        // VRAM/WRAM Control
        case 0x00'02'40:
            return sys->VRAMCR[0].Raw | (sys->VRAMCR[1].Raw << 8)
                | (sys->VRAMCR[2].Raw << 16) | (sys->VRAMCR[3].Raw << 24);
        case 0x00'02'44:
            return sys->VRAMCR[4].Raw | (sys->VRAMCR[5].Raw << 8)
                | (sys->VRAMCR[6].Raw << 16) | (sys->WRAMCR << 24);
        case 0x00'02'48:
            return sys->VRAMCR[7].Raw | (sys->VRAMCR[8].Raw << 8);


        case 0x00'02'80:
            return sys->DivCR.Raw;

        case 0x00'02'90:
            return sys->DivNum.b32[0];
        case 0x00'02'94:
            return sys->DivNum.b32[1];
        case 0x00'02'98:
            return sys->DivDen.b32[0];
        case 0x00'02'9C:
            return sys->DivDen.b32[1];
        case 0x00'02'A0:
            return sys->DivQuo.b32[0];
        case 0x00'02'A4:
            return sys->DivQuo.b32[1];
        case 0x00'02'A8:
            return sys->DivRem.b32[0];
        case 0x00'02'AC:
            return sys->DivRem.b32[1];

        case 0x00'03'04:
            return sys->PowerControl9.Raw;

        case 0x00'10'00:
            return sys->PPU_B.DisplayCR.Raw;

        case 0x00'10'08:
            return sys->PPU_B.BGCR[0].Raw | (sys->PPU_B.BGCR[1].Raw << 16);
        case 0x00'10'0C:
            return sys->PPU_B.BGCR[2].Raw | (sys->PPU_B.BGCR[3].Raw << 16);

        case 0x10'00'00:
            return IPC_FIFORead(sys, mask, true);

        default:
            LogPrint(LOG_ARM9 | LOG_UNIMP, "UNIMPLEMENTED IO9 READ: %08lX %08lX @ %08lX\n", addr, mask, sys->ARM9.ARM.PC);
            return 0;
    }
}

void IO9_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    switch (addr & 0xFF'FF'FC)
    {
        case 0x00'00'00:
            MaskedWrite(sys->PPU_A.DisplayCR.Raw, val, mask);
            break;

        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_Scanline, true);
            MaskedWrite(sys->DispStatRW.Raw, val, mask & 0xB8);

            // TODO VCount Match Settings.

            // CHECKME: byte writes probably behave weirdly.
            if ((mask == 0xFF000000) || (mask == 0x00FF0000)) LogPrint(LOG_UNIMP, "UNTESTED: VCOUNT BYTE WRITES!\n");
            if (mask & 0xFFFF0000)
            {
                sys->VCountUpdate = true;
                // CHECKME: how does this mask out?
                // CHECKME: what about > 262 vcounts?
                sys->VCountNew = ((val & mask) >> 16) & 0x1F;
            }
            break;
        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA9_IOWriteHandler(sys, sys->DMA9.Channels, addr, val, mask);
            break;
        case 0x00'00'E0 ... 0x00'00'EC:
            MaskedWrite(sys->DMAFill[(addr & 0xF) / 4], val, mask);
            break;

        case 0x00'01'00 ... 0x00'01'0C:
            Timer_IOWriteHandler(sys->Timers9, sys->AHB9.Timestamp, addr, val, mask);
            break;

        case 0x00'01'80: // ipcsync
        {
            AHB9_SyncWith7GT(sys);
            if (mask & 0xF00)
            {
                sys->IPCSyncDataTo7 = (val >> 8) & 0xF;
            }

            if ((val & mask & (1<<13)) && sys->IPCSyncIRQEnableTo7)
            {
                Console_ScheduleIRQs(sys, IRQ_IPCSync, false, sys->AHB9.Timestamp);
            }

            if (mask & (1<<14))
            {
                sys->IPCSyncIRQEnableTo9 = val & (1<<14);
            }
            break;
        }

        case 0x00'01'84:
            IPC_FIFOCRWrite(sys, val, mask, true);
            break;
        case 0x00'01'88:
            IPC_FIFOWrite(sys, val, mask, true);
            break;

        case 0x00'02'04: // exmemcnt
            MaskedWrite(sys->ExtMemCR_9.Raw, val, mask & 0x7F);
            bool membit1 = sys->ExtMemCR_Shared.MRSomething1;
            bool membit2 = sys->ExtMemCR_Shared.MRSomething2;

            // TODO: this mask should change with DSi features
            MaskedWrite(sys->ExtMemCR_Shared.Raw, val, mask & 0xE880);

            // these are probably write once...?
            sys->ExtMemCR_Shared.MRSomething1 |= membit1;
            sys->ExtMemCR_Shared.MRSomething2 |= membit2;
            break;

        case 0x00'02'08: // IME
            MaskedWrite(sys->IME9, val, mask & 1);
            break;
        case 0x00'02'10: // IE
            MaskedWrite(sys->IE9, val, mask & 0x003F3F7F);
            break;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_IF9Update, true);
            sys->IF9 &= ~(val & mask);
            break;

        // VRAM/WRAM Control
        // TODO: Does disabling a VRAM Bank actually decay bits? Test that pls.
        case 0x00'02'40:
        {
            if (mask & 0x000000FF)
            {
                sys->VRAMCR[0].Raw = val & 0x9B;
            }
            if (mask & 0x0000FF00)
            {
                sys->VRAMCR[1].Raw = (val >> 8) & 0x9B;
            }
            if (mask & 0x00FF0000)
            {
                sys->VRAMCR[2].Raw = (val >> 16) & 0x9F;
            }
            if (mask & 0xFF000000)
            {
                sys->VRAMCR[3].Raw = (val >> 24) & 0x9F;
            }
            break;
        }
        case 0x00'02'44:
        {
            if (mask & 0x000000FF)
            {
                sys->VRAMCR[4].Raw = val & 0x87;
            }
            if (mask & 0x0000FF00)
            {
                sys->VRAMCR[5].Raw = (val >> 8) & 0x9F;
            }
            if (mask & 0x00FF0000)
            {
                sys->VRAMCR[6].Raw = (val >> 16) & 0x9F;
            }
            if (mask & 0xFF000000)
            {
                sys->WRAMCR = (val >> 24) & 0x3;
            }
            break;
        }
        case 0x00'02'48:
        {
            if (mask & 0x000000FF)
            {
                sys->VRAMCR[7].Raw = val & 0x83;
            }
            if (mask & 0x0000FF00)
            {
                sys->VRAMCR[8].Raw = (val >> 8) & 0x83;
            }
            break;
        }


        case 0x00'02'80:
            MaskedWrite(sys->DivCR.Raw, val, mask & 0x3);
            IO9_StartDiv(sys);
            break;

        case 0x00'02'90:
            MaskedWrite(sys->DivNum.b32[0], val, mask);
            IO9_StartDiv(sys);
            break;
        case 0x00'02'94:
            MaskedWrite(sys->DivNum.b32[1], val, mask);
            IO9_StartDiv(sys);
            break;
        case 0x00'02'98:
            MaskedWrite(sys->DivDen.b32[0], val, mask);
            IO9_StartDiv(sys);
            break;
        case 0x00'02'9C:
            MaskedWrite(sys->DivDen.b32[1], val, mask);
            IO9_StartDiv(sys);
            break;


        case 0x00'03'04:
            MaskedWrite(sys->PowerControl9.Raw, val, mask & 0x820F);
            break;

        case 0x00'10'00:
            MaskedWrite(sys->PPU_B.DisplayCR.Raw, val, mask);
            break;

        case 0x00'10'08:
            MaskedWrite(sys->PPU_B.BGCR[0].Raw, val, mask);
            MaskedWrite(sys->PPU_B.BGCR[1].Raw, val>>16, mask>>16);
            break;
        case 0x00'10'0C:
            MaskedWrite(sys->PPU_B.BGCR[2].Raw, val, mask);
            MaskedWrite(sys->PPU_B.BGCR[3].Raw, val>>16, mask>>16);
            break;



        default:
            LogPrint(LOG_ARM9 | LOG_UNIMP, "UNIMPLEMENTED IO9 WRITE: %08lX %08lX %08lX @ %08lX\n", addr, val, mask, sys->ARM9.ARM.PC);
            break;
    }
}
