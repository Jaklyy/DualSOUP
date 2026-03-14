#include <string.h>
#include "../utils.h"
#include "../io/dma.h"
#include "../console.h"
#include "../sram/flash.h"
#include "../carts/gamecard.h"
#include "../io/powman.h"
#include "../video/video.h"
#include "../io/tsc.h"
#include "../io/sound.h"




// TODO: Regs im 99% confident about:
// Timers

u32 IPC_FIFORead(struct Console* sys, [[maybe_unused]] const u32 mask, const bool a9)
{
    struct IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    struct IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);
    timestamp ts = ((a9) ? sys->AHB9.Timestamp : sys->AHB7.Timestamp);

    if (a9) Console_SyncWith7GT(sys, ts, true);
    else    Console_SyncWith9GT(sys, ts, true);

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

    //printf("r%i %08X\n", a9, ret);
    return ret;
}

void IPC_FIFOWrite(struct Console* sys, const u32 val, const u32 mask, const bool a9)
{
    struct IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    struct IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);
    timestamp ts = ((a9) ? sys->AHB9.Timestamp : sys->AHB7.Timestamp);
    if (a9) Console_SyncWith7GT(sys, ts, true);
    else    Console_SyncWith9GT(sys, ts, true);

    //printf("w%i %08X\n", a9, val);

    // CHECKME: do both sides need to be enabled for it to work?
    // CHECKME: halfword/byte accesses?
    if (recv->CR.EnableFIFOs)
    {
        // CHECKME: does this write just get ignored or overwrite?
        if (recv->CR.SendFIFOFull)
        {
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

    if (a9) Console_SyncWith7GT(sys, ts, true);
    else    Console_SyncWith9GT(sys, ts, true);

    u16 old = recv->CR.Raw;
    MaskedWrite(recv->CR.Raw, val, mask & 0x8404);

    if (((old & 0x500) == 0x000) && recv->CR.RecvFIFONotEmptyIRQ) // checkme
        Console_ScheduleIRQs(sys, IRQ_IPCFIFONotEmpty, a9, ts);

    if (((old & 0x5) == 0x1) && recv->CR.SendFIFOEmptyIRQ) // checkme
        Console_ScheduleIRQs(sys, IRQ_IPCFIFOEmpty, a9, ts);

    // FIFO Flush; CHECKME: does this need power?
    if (mask & val & (1<<3))
    {
        memset(send->FIFO, 0, sizeof(sys->IPCFIFO7.FIFO));
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
        recv->CR.Error = false;
    }

}

void IO9_FinishDiv(struct Console* sys, [[maybe_unused]] timestamp now)
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

    Schedule_Event(sys, IO9_FinishDiv, Evt_Divider, timestamp_max);
}

void IO9_StartDiv(struct Console* sys)
{
    sys->DivQuo.b64 = 0;
    sys->DivRem.b64 = 0;
    sys->DivCR.Busy = true;
    Schedule_Event(sys, IO9_FinishDiv, Evt_Divider, sys->AHB9.Timestamp + ((sys->DivCR.DivMode == 0 ) ? 18 : 34));
}

// algorithm stolen from melonds which links this so im linking it too, sue me.
// one could also do this with 80 bit floats, but that's less portable.
// http://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2
void IO9_FinishSqrt(struct Console* sys, [[maybe_unused]] timestamp now)
{
    u64 val;
    u32 res = 0;
    u64 rem = 0;
    u32 prod = 0;
    int nbits, topshift;
    if (sys->SqrtCR.Use64Bits)
    {
        val = sys->SqrtParam.b64;
        nbits = 32;
        topshift = 62;
    }
    else
    {
        val = sys->SqrtParam.b32[0];
        nbits = 16;
        topshift = 30;
    }

    for (int i = 0; i < nbits; i++)
    {
        rem = (rem << 2) + ((val >> topshift) & 0x3);
        val <<= 2;
        res <<= 1;

        prod = (res << 1) + 1;
        if (rem >= prod)
        {
            rem -= prod;
            res++;
        }
    }

    sys->SqrtRes = res;
    sys->SqrtCR.Busy = false;
    Schedule_Event(sys, IO9_FinishSqrt, Evt_Sqrt, timestamp_max);
}

void IO9_StartSqrt(struct Console* sys)
{
    sys->SqrtRes = 0;
    sys->SqrtCR.Busy = true;
    Schedule_Event(sys, IO9_FinishSqrt, Evt_Sqrt, sys->AHB9.Timestamp + 13);
}

void SPI_Finish(struct Console* sys, timestamp cur)
{
    sys->SPIOut = sys->SPIBuf;
    sys->SPICR.Busy = false;
    if (sys->SPICR.IRQ) Console_ScheduleIRQs(sys, IRQ_SPI, false, cur); // delay?
    Schedule_Event(sys, SPI_Finish, Evt_SPI, timestamp_max);
}


u32 IO7_Read(struct Console* sys, const u32 addr, const u32 mask, const bool timings)
{
    Scheduler_TryRun(sys, false, sys->AHB7.Timestamp, true);
    //printf("ARM7IOREAD: %08X %08X\n", addr, mask);
    switch(addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Evt_Scanline, false, true);
            return (sys->VCount << 16) | sys->DispStatRO7.Raw | sys->DispStatRW7.Raw;

        case 0x00'00'B0 ... 0x00'00'E0-1:
            return DMA_IOReadHandler(&sys->DMA7.Channels[DMA7_NormalBase], addr);

        case 0x00'01'00 ... 0x00'01'0C:
            return Timer_IOReadHandler(sys, sys->AHB7.Timestamp, addr, false);

        case 0x00'01'30:
            return Input_PollMain(sys->Pad);

        case 0x00'01'34:
            u32 ret = sys->RCR | (Input_PollExtra(sys->Pad) << 16);
            return ret;

        case 0x00'01'38:
            return sys->RTC.CR.Raw;

        case 0x00'01'80: // ipcsync
            Console_SyncWith9GT(sys, sys->AHB7.Timestamp, true);
            return sys->IPCSyncDataTo7
                    | (sys->IPCSyncDataTo9 << 8)
                    | (sys->IPCSyncIRQEnableTo7 << 14);
        case 0x00'01'84:
            Console_SyncWith9GT(sys, sys->AHB7.Timestamp, true);
            return sys->IPCFIFO7.CR.Raw;

        case 0x00'01'A0 ... 0x00'01'B8:
            return Gamecard_IOReadHandler(sys, addr, sys->AHB7.Timestamp, false);

        case 0x00'01'C0:
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Evt_SPI, false, true);
            return sys->SPICR.Raw | (sys->SPIOut << 16);

        case 0x00'02'04: // External Memory Control
            Console_SyncWith9GT(sys, sys->AHB7.Timestamp, true);
            return sys->ExtMemCR_Shared.Raw | sys->ExtMemCR_7.Raw;

        case 0x00'02'08: // IME
            return sys->IME7;
        case 0x00'02'10: // IE
            return sys->IE7;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Evt_IF7Update, false, true);
            return sys->IF7;

        case 0x00'02'40: // VRAM/WRAM Status
            Console_SyncWith9GT(sys, sys->AHB7.Timestamp, true);
            return ((sys->VRAMCR[2].Raw & 0x87) == 0x82) | (((sys->VRAMCR[3].Raw & 0x87) == 0x82) << 1)
                   | sys->WRAMCR << 8;

        case 0x00'03'00:
            return sys->PostFlag;

        case 0x00'03'04:
            return sys->PowerCR7.Raw;


        case 0x00'04'00 ... 0x00'04'FC:
            return SoundChannel_IORead(sys, addr, sys->AHB7.Timestamp);

        case 0x00'05'00:
            return sys->SoundCR.Raw;
        case 0x00'05'04:
            return sys->SoundBias;

        case 0x00'05'08:
            return sys->SoundCaptures[0].CR.Raw | (sys->SoundCaptures[1].CR.Raw << 8);
        case 0x00'05'10:
            return sys->SoundCaptures[0].DstAddr;
        case 0x00'05'18:
            return sys->SoundCaptures[1].DstAddr;

        case 0x10'00'00:
            return IPC_FIFORead(sys, mask, false);

        case 0x10'00'10:
            return Gamecard_ROMDataRead(sys, sys->AHB7.Timestamp, false);


        default:
            if (timings) LogPrint(LOG_ARM7 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO7 READ: %08X %08X @ %08X\n", addr, mask, sys->ARM7.ARM.PC);
            return 0;
    }
}

void IO7_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask, const u32 a7pc)
{
    Scheduler_TryRun(sys, false, sys->AHB7.Timestamp, true);
    //printf("io7 %08X %08X %08X %08X\n", addr, val, mask, a7pc);
    switch(addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Evt_Scanline, false, true);
            MaskedWrite(sys->DispStatRW7.Raw, val, mask & 0xFFB8);
            sys->TargetVCount7 = (sys->DispStatRW7.VCountMSB << 8) | sys->DispStatRW7.VCountLSB;\

            if (mask & 0xFFFF0000)
            {
                sys->VCountUpdate7 = true;
                MaskedWrite(sys->VCountNew7, val>>16, (mask>>16) & 0x1FF);
            }
            break;

        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA7_IOWriteHandler(sys, &sys->DMA7.Channels[DMA7_NormalBase], addr, val, mask);
            break;

        case 0x00'01'00 ... 0x00'01'0C:
            Timer_IOWriteHandler(sys, sys->AHB7.Timestamp, addr, val, mask, false);
            break;

        case 0x00'01'34:
            MaskedWrite(sys->RCR, val, mask & 0x83);
            break;

        case 0x00'01'38:
            if (mask & 0x0000FFFF)
                RTC_IOWriteHandler(sys, val&0xFFFF,  mask&0xFFFF);
            break;

        case 0x00'01'80: // ipcsync
        {
            Console_SyncWith9GT(sys, sys->AHB7.Timestamp, true);
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

        case 0x00'01'A0 ... 0x00'01'B8:
            Gamecard_IOWriteHandler(sys, addr, val, mask, sys->AHB7.Timestamp, false);
            break;

        case 0x00'01'C0:
            MaskedWrite(sys->SPICR.Raw, val, mask & 0xCF83);

            if (mask & 0xFF0000)
            {
                switch(sys->SPICR.DeviceSelect)
                {
                case 0:
                    sys->SPIBuf = PowMan_CMDSend(sys, val>>16, sys->SPICR.ChipSelect);
                    break;
                case 1:
                    sys->SPIBuf = Flash_CMDSend(&sys->Firmware, val>>16, sys->SPICR.ChipSelect);
                    break;
                case 2:
                    sys->SPIBuf = TSC_SendCommand(&sys->TSC, val >> 16);
                    break;
                case 3:
                    LogPrint(LOG_ARM7|LOG_UNIMP, "spi RESERVED????????????\n");
                    break;
                }
                sys->SPICR.Busy = true;
                Schedule_Event(sys, SPI_Finish, Evt_SPI, sys->AHB7.Timestamp + (((8*8) << sys->SPICR.Baudrate))); // checkme: delay?
            }
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
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Evt_IF7Update, false, true);
            sys->IF7 &= ~(val & mask);
            sys->IF7 |= sys->IF7Held;
            break;


        case 0x00'03'00:
            if (a7pc < 0x4000) // can only be written from bios. for... some reason?
            {
                if (mask & 0x1) // post flag
                {
                    Console_SyncWith9GT(sys, sys->AHB7.Timestamp, true);
                    sys->PostFlag |= val & 0x1;
                }
                if (mask & 0xFF00) // wait control
                {
                    switch((val >> 14) & 0x3)
                    {
                    case 0: // does nothing
                        LogPrint(LOG_ARM7|LOG_ODD, "A7 wrote nothing to HaltCR...?\n");
                        break;
                    case 1: // GBA
                        LogPrint(LOG_ARM7|LOG_UNIMP, "But nobody came...\n\n\n...GBA mode unsupported, sorry!\n");
                        sys->ARM7.ARM.WaitForInterrupt = true;
                        break;
                    case 2: // halt
                        Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Evt_IF7Update, false, true);
                        if (!Console_CheckARM7Wake(sys)) // checkme: might still halt for a little?
                        {
                            sys->ARM7.ARM.WaitForInterrupt = true;
                        }
                        break;
                    case 3: // sleep
                        LogPrint(LOG_ARM7|LOG_UNIMP, "I dont really know what sleep does but it's not in yet!\n");
                        break;
                    }
                }
            }
            break;

        case 0x00'03'04:
            MaskedWrite(sys->PowerCR7.Raw, val, mask & 0x3);
            break;

        case 0x00'03'08:
            if ((a7pc < 0x4000) && (sys->Bios7Prot == 0)) // write once and can probably only be written from bios?
                MaskedWrite(sys->Bios7Prot, val, mask & 0xFFFC);
            break;


        case 0x00'04'00 ... 0x00'04'FC:
            SoundChannel_IOWrite(sys, addr, val, mask, sys->AHB7.Timestamp);
            break;

        case 0x00'05'00:
            if (!sys->PowerCR7.AudioPower) break; // read only
            u16 old = sys->SoundCR.Raw;
            MaskedWrite(sys->SoundCR.Raw, val, mask & 0xBF7F);
            /*if ((val ^ old) & 0x8000) // checkme?
            {
                if (val & 0x8000)
                {
                    printf("Sound Master enable\n");
                    SoundChannel_TryStartAll(sys, sys->AHB7.Timestamp);
                }
                else
                {
                    printf("Sound Master disable\n");
                    SoundChannel_KillAll(sys, sys->AHB7.Timestamp);
                }
            }*/
            break;

        case 0x00'05'04:
            if (!sys->PowerCR7.AudioPower) break; // read only
            MaskedWrite(sys->SoundBias, val, mask & 0x3FF);
            break;

        case 0x00'05'08:
            if (!sys->PowerCR7.AudioPower) break; // read only
            if (mask & 0x00FF)
            {
                SoundCapture_CRWrite(sys, val & 0xFF, sys->AHB7.Timestamp, 0);
            }
            if (mask & 0xFF00)
            {
                SoundCapture_CRWrite(sys, (val >> 8) & 0xFF, sys->AHB7.Timestamp, 1);
            }
            break;

        case 0x00'05'10:
            if (!sys->PowerCR7.AudioPower) break; // read only
            MaskedWrite(sys->SoundCaptures[0].DstAddr, val, mask & 0x07FFFFFC);
            break;
        case 0x00'05'14:
            if (!sys->PowerCR7.AudioPower) break; // read only
            MaskedWrite(sys->SoundCaptures[0].Length, val, mask & 0xFFFF);
            sys->DMA7.Channels[0+DMA7_SoundCapBase].NumWords = sys->SoundCaptures[0].Length + (sys->SoundCaptures[0].Length == 0);
            break;
        case 0x00'05'18:
            if (!sys->PowerCR7.AudioPower) break; // read only
            MaskedWrite(sys->SoundCaptures[1].DstAddr, val, mask & 0x07FFFFFC);
            break;
        case 0x00'05'1C:
            if (!sys->PowerCR7.AudioPower) break; // read only
            MaskedWrite(sys->SoundCaptures[1].Length, val, mask & 0xFFFF);
            sys->DMA7.Channels[1+DMA7_SoundCapBase].NumWords = sys->SoundCaptures[1].Length + (sys->SoundCaptures[1].Length == 0);
            break;


        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO7 WRITE: %08X %08X %08X @ %08X\n", addr, val, mask, sys->ARM7.ARM.PC);
            break;
    }
}

u32 IO9_Read(struct Console* sys, const u32 addr, const u32 mask, const bool timings)
{
    Scheduler_TryRun(sys, true, sys->AHB9.Timestamp, true);
    //printf("ARM9IOREAD: %08X %08X\n", addr, mask);
    switch (addr & 0xFF'FF'FC)
    {
        case 0x00'00'00:
            return sys->PPU_A.DisplayCR.Raw;

        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Scanline, true, true);
            return (sys->VCount << 16) | sys->DispStatRO9.Raw |  sys->DispStatRW9.Raw;

        case 0x00'00'08:
            return sys->PPU_A.BGCR[0].Raw | (sys->PPU_A.BGCR[1].Raw << 16);
        case 0x00'00'0C:
            return sys->PPU_A.BGCR[2].Raw | (sys->PPU_A.BGCR[3].Raw << 16);

        case 0x00'00'10:
            return sys->PPU_A.Xoff[0] | (sys->PPU_A.Yoff[0] << 16);
        case 0x00'00'14:
            return sys->PPU_A.Xoff[1] | (sys->PPU_A.Yoff[1] << 16);
        case 0x00'00'18:
            return sys->PPU_A.Xoff[2] | (sys->PPU_A.Yoff[2] << 16);
        case 0x00'00'1C:
            return sys->PPU_A.Xoff[3] | (sys->PPU_A.Yoff[3] << 16);

        case 0x00'00'60:
            return sys->GX3D.RasterCR.Raw;

        case 0x00'00'6C:
            return sys->PPU_A.Brightness.Raw;

        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            return DMA_IOReadHandler(sys->DMA9.Channels, addr);

        case 0x00'00'E0 ... 0x00'00'EC:
            return sys->DMAFill[(addr & 0xF) / 4];

        case 0x00'01'00 ... 0x00'01'0C:
            return Timer_IOReadHandler(sys, sys->AHB9.Timestamp, addr, true);

        case 0x00'01'30:
            return Input_PollMain(sys->Pad);

        // IPC
        case 0x00'01'80: // ipcsync
            Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
            return sys->IPCSyncDataTo9
                    | (sys->IPCSyncDataTo7 << 8)
                    | (sys->IPCSyncIRQEnableTo9 << 14);
        case 0x00'01'84:
            Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
            return sys->IPCFIFO9.CR.Raw;

        case 0x00'01'A0 ... 0x00'01'B8:
            return Gamecard_IOReadHandler(sys, addr, sys->AHB9.Timestamp, true);

        case 0x00'02'04: // External Memory Control
            return sys->ExtMemCR_Shared.Raw | sys->ExtMemCR_9.Raw;

        case 0x00'02'08: // IME
            return sys->IME9;
        case 0x00'02'10: // IE
            return sys->IE9;
        case 0x00'02'14: // IF
            // TODO: this should run more events?
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_IF9Update, true, true);
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
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivCR.Raw;

        case 0x00'02'90:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivNum.b32[0];
        case 0x00'02'94:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivNum.b32[1];
        case 0x00'02'98:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivDen.b32[0];
        case 0x00'02'9C:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivDen.b32[1];
        case 0x00'02'A0:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivQuo.b32[0];
        case 0x00'02'A4:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivQuo.b32[1];
        case 0x00'02'A8:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivRem.b32[0];
        case 0x00'02'AC:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->DivRem.b32[1];
        case 0x00'02'B0:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->SqrtCR.Raw;
        case 0x00'02'B4:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->SqrtRes;
        case 0x00'02'B8:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->SqrtParam.b32[0];
        case 0x00'02'BC:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Divider, true, true);
            return sys->SqrtParam.b32[1];


        case 0x00'03'04:
            return sys->PowerCR9.Raw;

        case 0x00'03'20 ... 0x00'03'FF:
            if (!sys->PowerCR9.GPURasterizerPower) return 0;
            return GX_IORead(sys, addr);
        case 0x00'04'00 ... 0x00'07'00:
            if (!sys->PowerCR9.GPUGeometryPower) return 0;
            return GX_IORead(sys, addr);


        case 0x00'10'00:
            return sys->PPU_B.DisplayCR.Raw;

        case 0x00'10'08:
            return sys->PPU_B.BGCR[0].Raw | (sys->PPU_B.BGCR[1].Raw << 16);
        case 0x00'10'0C:
            return sys->PPU_B.BGCR[2].Raw | (sys->PPU_B.BGCR[3].Raw << 16);

        case 0x00'10'10:
            return sys->PPU_B.Xoff[0] | (sys->PPU_B.Yoff[0] << 16);
        case 0x00'10'14:
            return sys->PPU_B.Xoff[1] | (sys->PPU_B.Yoff[1] << 16);
        case 0x00'10'18:
            return sys->PPU_B.Xoff[2] | (sys->PPU_B.Yoff[2] << 16);
        case 0x00'10'1C:
            return sys->PPU_B.Xoff[3] | (sys->PPU_B.Yoff[3] << 16);

        case 0x00'10'6C:
            return sys->PPU_B.Brightness.Raw;

        case 0x00'03'00:
            Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
            return sys->PostFlag | (sys->PostFlagA9Bit << 1);

        case 0x10'00'00:
            return IPC_FIFORead(sys, mask, true);

        case 0x10'00'10:
            return Gamecard_ROMDataRead(sys, sys->AHB9.Timestamp, true);

        default:
            if (timings) LogPrint(LOG_ARM9 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO9 READ: %08X %08X @ %08X\n", addr, mask, sys->ARM9.ARM.PC);
            return 0;
    }
}

void IO9_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    Scheduler_TryRun(sys, true, sys->AHB9.Timestamp, true);
    //printf("ARM9IOWRITE: %08X %08X %08X\n", addr, mask, val);
    switch (addr & 0xFF'FF'FC)
    {
        case 0x00'00'00:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.DisplayCR.Raw, val, mask);
            break;

        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_Scanline, true, true);
            MaskedWrite(sys->DispStatRW9.Raw, val, mask & 0xFFB8);
            sys->TargetVCount9 = (sys->DispStatRW9.VCountMSB << 8) | sys->DispStatRW9.VCountLSB;

            if (mask & 0xFFFF0000)
            {
                sys->VCountUpdate9 = true;
                MaskedWrite(sys->VCountNew9, val>>16, (mask>>16) & 0x1FF);
            }
            break;

        case 0x00'00'08:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.BGCR[0].Raw, val, mask);
            MaskedWrite(sys->PPU_A.BGCR[1].Raw, val>>16, mask>>16);
            break;
        case 0x00'00'0C:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.BGCR[2].Raw, val, mask);
            MaskedWrite(sys->PPU_A.BGCR[3].Raw, val>>16, mask>>16);
            break;

        case 0x00'00'10:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.Xoff[0], val, mask&0x1FF);
            MaskedWrite(sys->PPU_A.Yoff[0], val>>16, (mask>>16)&0x1FF);
            break;
        case 0x00'00'14:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.Xoff[1], val, mask&0x1FF);
            MaskedWrite(sys->PPU_A.Yoff[1], val>>16, (mask>>16)&0x1FF);
            break;
        case 0x00'00'18:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.Xoff[2], val, mask&0x1FF);
            MaskedWrite(sys->PPU_A.Yoff[2], val>>16, (mask>>16)&0x1FF);
            break;
        case 0x00'00'1C:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.Xoff[3], val, mask&0x1FF);
            MaskedWrite(sys->PPU_A.Yoff[3], val>>16, (mask>>16)&0x1FF);
            break;

        case 0x00'00'60:
            MaskedWrite(sys->GX3D.RasterCR.Raw, val, mask & 0x4FFF);
            break;

        case 0x00'00'6C:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_A.Brightness.Raw, val, mask & 0xC01F);
            break;

        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA9_IOWriteHandler(sys, sys->DMA9.Channels, addr, val, mask);
            break;
        case 0x00'00'E0 ... 0x00'00'EC:
            MaskedWrite(sys->DMAFill[(addr & 0xF) / 4], val, mask);
            break;

        case 0x00'01'00 ... 0x00'01'0C:
            Timer_IOWriteHandler(sys, sys->AHB9.Timestamp, addr, val, mask, true);
            break;

        case 0x00'01'80: // ipcsync
        {
            Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
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

        case 0x00'01'A0 ... 0x00'01'B8:
            Gamecard_IOWriteHandler(sys, addr, val, mask, sys->AHB9.Timestamp, true);
            break;

        case 0x00'02'04: // exmemcnt
            Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
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
            // TODO: this should run more events?
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Evt_IF9Update, true, true);
            sys->IF9 &= ~(val & mask);
            sys->IF9 |= sys->IF9Held;
            break;

        // VRAM/WRAM Control
        // TODO: Does disabling a VRAM Bank actually decay bits? Test that pls.
        case 0x00'02'40:
        {
            PPU_Sync(sys, sys->AHB9.Timestamp);
            if (mask & 0x000000FF)
            {
                union VRAMCR new = {.Raw = val & 0x9B};
                if ((sys->VRAMCR[0].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                sys->VRAMCR[0] = new;
            }
            if (mask & 0x0000FF00)
            {
                union VRAMCR new = {.Raw = (val>>8) & 0x9B};
                if ((sys->VRAMCR[1].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                sys->VRAMCR[1] = new;
            }
            if (mask & 0x00FF0000)
            {
                union VRAMCR new = {.Raw = (val>>16) & 0x9F};
                if ((sys->VRAMCR[2].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                if ((sys->VRAMCR[2].Mode == 2) || (new.Mode == 2)) Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
                sys->VRAMCR[2] = new;
            }
            if (mask & 0xFF000000)
            {
                union VRAMCR new = {.Raw = (val>>24) & 0x9F};
                if ((sys->VRAMCR[3].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                if ((sys->VRAMCR[3].Mode == 2) || (new.Mode == 2)) Console_SyncWith7GT(sys, sys->AHB9.Timestamp, true);
                sys->VRAMCR[3] = new;
            }
            break;
        }
        case 0x00'02'44:
        {
            PPU_Sync(sys, sys->AHB9.Timestamp);
            if (mask & 0x000000FF)
            {
                union VRAMCR new = {.Raw = val & 0x87};
                if ((sys->VRAMCR[4].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                sys->VRAMCR[4] = new;
            }
            if (mask & 0x0000FF00)
            {
                union VRAMCR new = {.Raw = (val>>8) & 0x9F};
                if ((sys->VRAMCR[5].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                sys->VRAMCR[5] = new;
            }
            if (mask & 0x00FF0000)
            {
                union VRAMCR new = {.Raw = (val>>16) & 0x9F};
                if ((sys->VRAMCR[6].Mode == 3) || (new.Mode == 3)) SWRen_Sync(sys, sys->AHB9.Timestamp);
                sys->VRAMCR[6] = new;
            }
            if (mask & 0xFF000000)
            {
                sys->WRAMCR = (val >> 24) & 0x3;
            }
            break;
        }
        case 0x00'02'48:
        {
            PPU_Sync(sys, sys->AHB9.Timestamp);
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


        // division
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
        // square root
        case 0x00'02'B0:
            MaskedWrite(sys->SqrtCR.Raw, val, mask & 1);
            IO9_StartSqrt(sys);
            break;
        case 0x00'02'B8:
            MaskedWrite(sys->SqrtParam.b32[0], val, mask);
            IO9_StartSqrt(sys);
            break;
        case 0x00'02'BC:
            MaskedWrite(sys->SqrtParam.b32[1], val, mask);
            IO9_StartSqrt(sys);
            break;



        case 0x00'03'00:
            if (mask & 2) sys->PostFlagA9Bit = val & 2;
            break;

        case 0x00'03'04:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            SWRen_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PowerCR9.Raw, val, mask & 0x820F);
            break;

        case 0x00'03'20 ... 0x00'03'FF:
            if (!sys->PowerCR9.GPURasterizerPower) break;
            GX_IOWrite(sys, addr, mask, val);
            break;
        case 0x00'04'00 ... 0x00'07'00:
            if (!sys->PowerCR9.GPUGeometryPower) break;
            GX_IOWrite(sys, addr, mask, val);
            break;

        case 0x00'10'00:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.DisplayCR.Raw, val, mask);
            break;

        case 0x00'10'08:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.BGCR[0].Raw, val, mask);
            MaskedWrite(sys->PPU_B.BGCR[1].Raw, val>>16, mask>>16);
            break;

        case 0x00'10'0C:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.BGCR[2].Raw, val, mask);
            MaskedWrite(sys->PPU_B.BGCR[3].Raw, val>>16, mask>>16);
            break;

        case 0x00'10'10:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.Xoff[0], val, mask&0x1FF);
            MaskedWrite(sys->PPU_B.Yoff[0], val>>16, (mask>>16)&0x1FF);
            break;
        case 0x00'10'14:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.Xoff[1], val, mask&0x1FF);
            MaskedWrite(sys->PPU_B.Yoff[1], val>>16, (mask>>16)&0x1FF);
            break;
        case 0x00'10'18:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.Xoff[2], val, mask&0x1FF);
            MaskedWrite(sys->PPU_B.Yoff[2], val>>16, (mask>>16)&0x1FF);
            break;
        case 0x00'10'1C:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.Xoff[3], val, mask&0x1FF);
            MaskedWrite(sys->PPU_B.Yoff[3], val>>16, (mask>>16)&0x1FF);
            break;

        case 0x00'10'6C:
            PPU_Sync(sys, sys->AHB9.Timestamp);
            MaskedWrite(sys->PPU_B.Brightness.Raw, val, mask & 0xC01F);
            break;


        default:
            LogPrint(LOG_ARM9 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO9 WRITE: %08X %08X %08X @ %08X\n", addr, val, mask, sys->ARM9.ARM.PC);
            break;
    }
}
