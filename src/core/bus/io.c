#include <string.h>
#include "core/utils.h"
#include "core/io/dma.h"
#include "core/console.h"
#include "core/sram/flash.h"
#include "core/carts/gamecard.h"
#include "core/io/pmic.h"
#include "core/video/video.h"
#include "core/io/tsc.h"
#include "core/io/sound.h"
#include "core/scheduler.h"




// TODO: Regs im 99% confident about:
// Timers (edit: its really funny how i wrote this during the period of time that performance was completely crippled by timer dividers not working properly)

void IPC_FIFOInit(IPCFIFO* fifo)
{
    fifo->CR.RecvFIFOEmpty = true;
    fifo->CR.SendFIFOEmpty = true;
}

u32 IPC_FIFORead(Console* sys, const timestamp now, const bool a9)
{
    IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);

    u32 ret;
    // CHECKME: do both sides need to be enabled for it to work?
    // CHECKME: halfword/byte accesses?
    if (recv->CR.EnableFIFOs)
    {
        // return last value.
        if (recv->CR.RecvFIFOEmpty)
        {
            recv->CR.Error = true;
            return recv->FIFO[(recv->DrainPtr-1) % countof(recv->FIFO)];
        }

        ret = recv->FIFO[recv->DrainPtr];

        // no longer full
        if (recv->CR.RecvFIFOFull)
        {
            recv->CR.RecvFIFOFull = false;
            send->CR.SendFIFOFull = false;
        }

        recv->DrainPtr = (recv->DrainPtr + 1) % countof(recv->FIFO);

        // now empty
        if (recv->FillPtr == recv->DrainPtr)
        {
            recv->CR.RecvFIFOEmpty = true;
            send->CR.SendFIFOEmpty = true;
            // send irq
            if (send->CR.SendFIFOEmptyIRQ)
                Sched_AddEvent(sys, now+DSClk33(1) /*checkme: delay?*/, (!a9 ? Evt_IRQ9_IPCFIFOEmpty : Evt_IRQ7_IPCFIFOEmpty));
        }
    }
    else ret = recv->FIFO[recv->DrainPtr]; // return oldest value.
    return ret;
}

void IPC_FIFOWrite(Console* sys, const u32 val, const u32 mask, const timestamp now, const bool a9)
{
    IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);

    // CHECKME: do both sides need to be enabled for it to work?
    // CHECKME: halfword/byte accesses?
    if (recv->CR.EnableFIFOs)
    {
        // CHECKME: does this write just get ignored or overwrite?
        if (recv->CR.SendFIFOFull) return;

        MaskedWrite(send->FIFO[send->FillPtr], val, mask);

        // no longer empty
        if (recv->CR.SendFIFOEmpty)
        {
            send->CR.RecvFIFOEmpty = false;
            recv->CR.SendFIFOEmpty = false;
            // send irq
            if (send->CR.RecvFIFONotEmptyIRQ)
            Sched_AddEvent(sys, now+DSClk33(1) /*checkme: delay?*/, (!a9 ? Evt_IRQ9_IPCFIFONotEmpty : Evt_IRQ7_IPCFIFONotEmpty));
        }
        send->FillPtr = (send->FillPtr + 1) % countof(send->FIFO);

        // now full
        if (send->FillPtr == send->DrainPtr)
        {
            send->CR.RecvFIFOFull = true;
            recv->CR.SendFIFOFull = true;
        }
    }
}

void IPC_FIFOCRWrite(Console* sys, const u32 val, const u32 mask, const timestamp now, const bool a9)
{
    IPCFIFO* send = ((a9) ? &sys->IPCFIFO7 : &sys->IPCFIFO9);
    IPCFIFO* recv = ((a9) ? &sys->IPCFIFO9 : &sys->IPCFIFO7);

    u16 old = recv->CR.Raw;
    MaskedWrite(recv->CR.Raw, val, mask & 0x8404);

    // raise irqs on enable if their conditions are met
    if (((old & 0x500) == 0x000) && recv->CR.RecvFIFONotEmptyIRQ) // checkme
        Sched_AddEvent(sys, now+DSClk33(1) /*checkme: delay?*/, (a9 ? Evt_IRQ9_IPCFIFONotEmpty : Evt_IRQ7_IPCFIFONotEmpty));
    if (((old & 0x5) == 0x1) && recv->CR.SendFIFOEmptyIRQ) // checkme
        Sched_AddEvent(sys, now+DSClk33(1) /*checkme: delay?*/, (a9 ? Evt_IRQ9_IPCFIFOEmpty : Evt_IRQ7_IPCFIFOEmpty));

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
            Sched_AddEvent(sys, now+DSClk33(1) /*checkme: delay?*/, (!a9 ? Evt_IRQ9_IPCFIFOEmpty : Evt_IRQ7_IPCFIFOEmpty));
    }

    // ack error
    if (mask & val & (1<<14)) recv->CR.Error = false;
}

void IO9_FinishDiv(Console* sys)
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
}

void IO9_StartDiv(Console* sys, timestamp now)
{
    sys->DivQuo.b64 = 0;
    sys->DivRem.b64 = 0;
    sys->DivCR.Busy = true;
    Sched_AddEvent(sys, now + DSClk33((sys->DivCR.DivMode == 0 ) ? 18 : 34), Evt_Divider);
}

// algorithm stolen from melonds which links this so im linking it too, sue me.
// one could also do this with 80 bit floats, but that's less portable.
// http://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2
void IO9_FinishSqrt(Console* sys)
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
}

void IO9_StartSqrt(Console* sys, timestamp now)
{
    sys->SqrtRes = 0;
    sys->SqrtCR.Busy = true;
    Sched_AddEvent(sys, now + DSClk33(13), Evt_Sqrt);
}

void SPI_Finish(Console* sys, timestamp now)
{
    sys->SPIOut = sys->SPIBuf;
    sys->SPICR.Busy = false;
    if (sys->SPICR.IRQ) Sched_AddEvent(sys, now+DSClk33(1), Evt_IRQ7_SPI); // delay?
}

void IO9_Read(Console* sys, const u32 addr, const timestamp now, const BusCallbacks cb, u8 man)
{
    u32 rdata;
    switch (addr & 0xFF'FF'FC)
    {
    // video A
    case 0x00'00'00: // dispcnt
    case 0x00'00'08 ... 0x00'00'54: // ppu/lcd a block
    case 0x00'00'6C: // bright a
        rdata = PPU_IORead(&sys->PPU_A, addr);  break;
    case 0x00'00'04: rdata = (sys->VCount << 16) | sys->DispStatRO9.Raw | sys->DispStatRW9.Raw; break;
    case 0x00'00'60: rdata = sys->GX3D.RasterCR.Raw; break;

    case 0x00'00'B0 ... 0x00'00'E0-1: rdata = DMA_IOReadHandler(sys->DMA9.Channels, addr); break;
    case 0x00'00'E0 ... 0x00'00'EC: rdata = sys->DMAFill[(addr & 0xF) / 4]; break;
    case 0x00'01'00 ... 0x00'01'0C: rdata = Timer_IOReadHandler(sys, now, addr, true); break;
    case 0x00'01'30: rdata = Input_PollMain(sys->Pad); break;

    case 0x00'01'80: rdata = sys->IPCSyncDataTo9 | (sys->IPCSyncDataTo7 << 8) | (sys->IPCSyncIRQEnableTo9 << 14); break;

    case 0x00'01'84: rdata = sys->IPCFIFO9.CR.Raw; break;

    case 0x00'01'A0 ... 0x00'01'B8: rdata = GameCard_IOReadHandler(sys, addr, true); break;

    case 0x00'02'04: rdata = sys->ExtMemCR_Shared.Raw | sys->ExtMemCR_9.Raw; break;

    case 0x00'02'08: rdata = sys->IME9; break;
    case 0x00'02'10: rdata = sys->IE9; break;
    case 0x00'02'14: rdata = sys->IF9; break;

    // internal memory control
    case 0x00'02'40:
        rdata = (sys->VRAMCR[VRAMID_A].Raw << 0 ) | (sys->VRAMCR[VRAMID_B].Raw << 8 )
                | (sys->VRAMCR[VRAMID_C].Raw << 16) | (sys->VRAMCR[VRAMID_D].Raw << 24);
        break;
    case 0x00'02'44:
        rdata = (sys->VRAMCR[VRAMID_E].Raw << 0 ) | (sys->VRAMCR[VRAMID_F].Raw << 8 )
                | (sys->VRAMCR[VRAMID_G].Raw << 16) | (sys->WRAMCR               << 24);
        break;
    case 0x00'02'48:
        rdata = (sys->VRAMCR[VRAMID_H].Raw << 0 ) | (sys->VRAMCR[VRAMID_I].Raw << 8);
        break;

    // hardware divider
    case 0x00'02'80: rdata = sys->DivCR.Raw; break;

    case 0x00'02'90: rdata = sys->DivNum.b32[0]; break;
    case 0x00'02'94: rdata = sys->DivNum.b32[1]; break;

    case 0x00'02'98: rdata = sys->DivDen.b32[0]; break;
    case 0x00'02'9C: rdata = sys->DivDen.b32[1]; break;

    case 0x00'02'A0: rdata = sys->DivQuo.b32[0]; break;
    case 0x00'02'A4: rdata = sys->DivQuo.b32[1]; break;

    case 0x00'02'A8: rdata = sys->DivRem.b32[0]; break;
    case 0x00'02'AC: rdata = sys->DivRem.b32[1]; break;

    // hardware square root
    case 0x00'02'B0: rdata = sys->SqrtCR.Raw;       break;
    case 0x00'02'B4: rdata = sys->SqrtRes;          break;
    case 0x00'02'B8: rdata = sys->SqrtParam.b32[0]; break;
    case 0x00'02'BC: rdata = sys->SqrtParam.b32[1]; break;

    case 0x00'03'04: rdata = sys->PowerCR9.Raw; break;

    case 0x00'03'20 ... 0x00'03'FF: // 3d rasterizer io block
        if (!sys->PowerCR9.GPURasterizerPower) rdata = 0;
        else rdata = GX_IORead(sys, addr);
        break;

    case 0x00'04'00 ... 0x00'07'00: // 3d geometry io block
        if (!sys->PowerCR9.GPUGeometryPower) rdata = 0;
        else rdata = GX_IORead(sys, addr);
        break;

    case 0x00'10'00 ... 0x00'10'6C: rdata = PPU_IORead(&sys->PPU_B, addr); break;

    case 0x00'03'00: rdata = sys->PostFlag | (sys->PostFlagA9Bit << 1); break;

    case 0x10'00'00: rdata = IPC_FIFORead(sys, now, true); break;
    case 0x10'00'10: rdata = GameCard_ROMDataRead(sys, now, true); break;

    default: // unmapped io
        LogPrint(LOG_ARM9 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO9 READ: %08"PRIX32" @ %08"PRIX32"\n", addr, sys->A946ES.ARM.PC);
        rdata = 0;
        break;
    }
    Bus_TransferPostSetup(sys, rdata, true, now + DSClk33(1), false, cb, man, true);
}

void IO9_Write(Console* sys, const u32 addr, timestamp now, const u32 wrdata, const u32 mask, const BusCallbacks cb, u8 man)
{
    switch (addr & 0xFF'FF'FC)
    {
    case 0x00'00'00:
    case 0x00'00'08 ... 0x00'00'54:
    case 0x00'00'6C:
        PPU_Sync(sys, now);
        PPU_IOWrite(&sys->PPU_A, addr, wrdata, mask, false, sys->PowerCR9.PPUAPower);
        break;

    case 0x00'00'04:
        MaskedWrite(sys->DispStatRW9.Raw, wrdata, mask & 0xFFB8);
        sys->TargetVCount9 = (sys->DispStatRW9.VCountMSB << 8) | sys->DispStatRW9.VCountLSB;

        if (mask & 0xFFFF0000)
        {
            sys->VCountUpdate9 = true;
            MaskedWrite(sys->VCountNew9, wrdata>>16, (mask>>16) & 0x1FF);
        }
        break;

    case 0x00'00'60: MaskedWrite(sys->GX3D.RasterCR.Raw, wrdata, mask & 0x4FFF); break;

    // DMA
    case 0x00'00'B0 ... 0x00'00'E0-1: DMA9_IOWriteHandler(sys, now, sys->DMA9.Channels, addr, wrdata, mask); break;
    case 0x00'00'E0 ... 0x00'00'EC: MaskedWrite(sys->DMAFill[(addr & 0xF) / 4], wrdata, mask); break;

    case 0x00'01'00 ... 0x00'01'0C: Timer_IOWriteHandler(sys, now, addr, wrdata, mask, true); break;

    case 0x00'01'80: // ipcsync
        if (mask & 0xF00) sys->IPCSyncDataTo7 = (wrdata >> 8) & 0xF;

        if ((wrdata & mask & (1<<13)) && sys->IPCSyncIRQEnableTo7)
            Sched_AddEvent(sys, now + DSClk33(1), Evt_IRQ7_IPCSync);

        if (mask & (1<<14)) sys->IPCSyncIRQEnableTo9 = wrdata & (1<<14);
        break;

    case 0x00'01'84: IPC_FIFOCRWrite(sys, wrdata, mask, now, true); break;
    case 0x00'01'88: IPC_FIFOWrite(sys, wrdata, mask, now, true); break;

    case 0x00'01'A0 ... 0x00'01'B8: GameCard_IOWriteHandler(sys, addr, wrdata, mask, now, true); break;

    case 0x00'02'04: // exmemcnt
        MaskedWrite(sys->ExtMemCR_9.Raw, wrdata, mask & 0x7F);
        MaskedWrite(sys->ExtMemCR_Shared.Raw, wrdata, mask & 0x8880); // TODO: this mask should be 0x8CFF for DSi cut second card slot
        sys->ExtMemCR_Shared.Raw |= wrdata & mask & 0x6000; // Main RAM Bits; these are probably write once...?
        break;

    case 0x00'02'08: MaskedWrite(sys->IME9, wrdata, mask & 1); break;
    case 0x00'02'10: MaskedWrite(sys->IE9, wrdata, mask & 0x003F3F7F); break;
    case 0x00'02'14: IF9_Clear(sys, wrdata, now); break;

    // VRAM/WRAM Control
    // TODO: Does disabling a VRAM Bank actually decay bits? Test that pls.
    case 0x00'02'40:
    {
        PPU_Sync(sys, now);
        if (mask & 0x000000FF)
        {
            VRAMCR new = {.Raw = wrdata & 0x9B};
            if ((sys->VRAMCR[VRAMID_A].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_A] = new;
        }
        if (mask & 0x0000FF00)
        {
            VRAMCR new = {.Raw = (wrdata>>8) & 0x9B};
            if ((sys->VRAMCR[VRAMID_B].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_B] = new;
        }
        if (mask & 0x00FF0000)
        {
            VRAMCR new = {.Raw = (wrdata>>16) & 0x9F};
            if (!(sys->VRAMCR[VRAMID_C].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_C] = new;
        }
        if (mask & 0xFF000000)
        {
            VRAMCR new = {.Raw = (wrdata>>24) & 0x9F};
            if ((sys->VRAMCR[VRAMID_D].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_D] = new;
        }
        break;
    }
    case 0x00'02'44:
    {
        PPU_Sync(sys, now);
        if (mask & 0x000000FF)
        {
            VRAMCR new = {.Raw = wrdata & 0x87};
            if ((sys->VRAMCR[VRAMID_E].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_E] = new;
        }
        if (mask & 0x0000FF00)
        {
            VRAMCR new = {.Raw = (wrdata>>8) & 0x9F};
            if ((sys->VRAMCR[VRAMID_F].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_F] = new;
        }
        if (mask & 0x00FF0000)
        {
            VRAMCR new = {.Raw = (wrdata>>16) & 0x9F};
            if ((sys->VRAMCR[VRAMID_G].Mode == 3) != (new.Mode == 3)) SWRen_Sync(sys, now);
            sys->VRAMCR[VRAMID_G] = new;
        }
        if (mask & 0xFF000000) sys->WRAMCR = (wrdata >> 24) & 0x3;
        break;
    }
    case 0x00'02'48:
    {
        PPU_Sync(sys, now);
        if (mask & 0x000000FF) sys->VRAMCR[VRAMID_H].Raw = wrdata & 0x83;
        if (mask & 0x0000FF00) sys->VRAMCR[VRAMID_I].Raw = (wrdata >> 8) & 0x83;
        break;
    }

    // division
    case 0x00'02'80:
        MaskedWrite(sys->DivCR.Raw, wrdata, mask & 0x3);
        IO9_StartDiv(sys, now); // checkme: does it restart if no changes were made? does writing the high bits restart?
        break;
    case 0x00'02'90:
        MaskedWrite(sys->DivNum.b32[0], wrdata, mask);
        IO9_StartDiv(sys, now);
        break;
    case 0x00'02'94:
        MaskedWrite(sys->DivNum.b32[1], wrdata, mask);
        IO9_StartDiv(sys, now);
        break;
    case 0x00'02'98:
        MaskedWrite(sys->DivDen.b32[0], wrdata, mask);
        IO9_StartDiv(sys, now);
        break;
    case 0x00'02'9C:
        MaskedWrite(sys->DivDen.b32[1], wrdata, mask);
        IO9_StartDiv(sys, now);
        break;
    // square root
    case 0x00'02'B0:
        MaskedWrite(sys->SqrtCR.Raw, wrdata, mask & 1);
        IO9_StartSqrt(sys, now);
        break;
    case 0x00'02'B8:
        MaskedWrite(sys->SqrtParam.b32[0], wrdata, mask);
        IO9_StartSqrt(sys, now);
        break;
    case 0x00'02'BC:
        MaskedWrite(sys->SqrtParam.b32[1], wrdata, mask);
        IO9_StartSqrt(sys, now);
        break;

    case 0x00'03'00:
        if (mask & 2) sys->PostFlagA9Bit = wrdata & 2;
        break;

    case 0x00'03'04:
        PPU_Sync(sys, now);
        SWRen_Sync(sys, now);
        MaskedWrite(sys->PowerCR9.Raw, wrdata, mask & 0x820F);
        break;

    case 0x00'03'20 ... 0x00'03'FC:
        if (sys->PowerCR9.GPURasterizerPower) GX_IOWrite(sys, addr, mask, wrdata);
        break;
    case 0x00'04'00 ... 0x00'06'FC:
        if (sys->PowerCR9.GPUGeometryPower) GX_IOWrite(sys, addr, mask, wrdata);
        break;

    case 0x00'10'00 ... 0x00'10'6C:
        PPU_Sync(sys, now);
        PPU_IOWrite(&sys->PPU_B, addr, wrdata, mask, true, sys->PowerCR9.PPUBPower);
        break;

    default:
        LogPrint(LOG_ARM9 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO9 WRITE: %08"PRIX32" %08"PRIX32" %08"PRIX32" @ %08"PRIX32"\n", addr, wrdata, mask, sys->A946ES.ARM.PC);
        break;
    }
    now += DSClk33(1);
    AddBusContention(sys, now, Dev_IO9);
    Bus_TransferPostSetup(sys, 0, false, now, false, cb, man, true);
}

void IO7_Read(Console* sys, const u32 addr, const timestamp now, const BusCallbacks cb, u8 man)
{
    u32 rdata;
    switch(addr & 0xFF'FF'FC)
    {
    case 0x00'00'04: rdata = (sys->VCount << 16) | sys->DispStatRO7.Raw | sys->DispStatRW7.Raw; break;

    case 0x00'00'B0 ... 0x00'00'E0-1: rdata = DMA_IOReadHandler(&sys->DMA7.Channels[DMA7_NormalBase], addr); break;

    case 0x00'01'00 ... 0x00'01'0C: rdata = Timer_IOReadHandler(sys, now, addr, false); break;

    case 0x00'01'30:rdata = Input_PollMain(sys->Pad); break;

    case 0x00'01'34:
        rdata = sys->RCR | (Input_PollExtra(sys->TSC.State.Touched, sys->Pad) << 16); break;

    case 0x00'01'38: rdata = sys->RTC.CR.Raw; break;

    case 0x00'01'80: // ipcsync
        rdata = sys->IPCSyncDataTo7
                | (sys->IPCSyncDataTo9 << 8)
                | (sys->IPCSyncIRQEnableTo7 << 14);
        break;
    case 0x00'01'84: rdata = sys->IPCFIFO7.CR.Raw; break;

    case 0x00'01'A0 ... 0x00'01'B8: rdata = GameCard_IOReadHandler(sys, addr, false); break;

    case 0x00'01'C0: rdata = sys->SPICR.Raw | (sys->SPIOut << 16); break;

    case 0x00'02'04: rdata = sys->ExtMemCR_Shared.Raw | sys->ExtMemCR_7.Raw; break;

    case 0x00'02'08: rdata = sys->IME7; break;
    case 0x00'02'10: rdata = sys->IE7; break;
    case 0x00'02'14: rdata = sys->IF7; break;

    case 0x00'02'40: rdata = ((sys->VRAMCR[VRAMID_C].Raw & 0x87) == 0x82) | (((sys->VRAMCR[VRAMID_D].Raw & 0x87) == 0x82) << 1) | (sys->WRAMCR << 8); break;

    case 0x00'03'00: rdata = sys->PostFlag; break;

    case 0x00'03'04: rdata = sys->PowerCR7.Raw; break;


    case 0x00'04'00 ... 0x00'04'FC: rdata = SoundChannel_IORead(sys, addr); break;

    case 0x00'05'00: rdata = sys->SoundCR.Raw; break;
    case 0x00'05'04: rdata = sys->SoundBias; break;

    case 0x00'05'08: rdata = sys->SoundCaptures[0].CR.Raw | (sys->SoundCaptures[1].CR.Raw << 8); break;
    case 0x00'05'10: rdata = sys->SoundCaptures[0].DstAddr; break;
    case 0x00'05'18: rdata = sys->SoundCaptures[1].DstAddr; break;

    case 0x10'00'00: rdata = IPC_FIFORead(sys, now, false); break;

    case 0x10'00'10: rdata = GameCard_ROMDataRead(sys, now, false); break;

    default:
        LogPrint(LOG_ARM7 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO7 READ: %08"PRIX32" @ %08"PRIX32"\n", addr, sys->A7TDMI.ARM.PC);
        rdata = 0;
        break;
    }
    Bus_TransferPostSetup(sys, rdata, true, now + DSClk33(1), false, cb, man, false);
}

void IO7_Write(Console* sys, const u32 addr, timestamp now, const u32 wrdata, const u32 mask, const BusCallbacks cb, u8 man)
{
    switch(addr & 0xFF'FF'FC)
    {
    case 0x00'00'04:
        MaskedWrite(sys->DispStatRW7.Raw, wrdata, mask & 0xFFB8);
        sys->TargetVCount7 = (sys->DispStatRW7.VCountMSB << 8) | sys->DispStatRW7.VCountLSB;\

        if (mask & 0xFFFF0000)
        {
            sys->VCountUpdate7 = true;
            MaskedWrite(sys->VCountNew7, wrdata>>16, (mask>>16) & 0x1FF);
        }
        break;

    case 0x00'00'B0 ... 0x00'00'E0-1: DMA7_IOWriteHandler(sys, now, &sys->DMA7.Channels[DMA7_NormalBase], addr, wrdata, mask); break;

    case 0x00'01'00 ... 0x00'01'0C: Timer_IOWriteHandler(sys, now, addr, wrdata, mask, false); break;

    case 0x00'01'34: MaskedWrite(sys->RCR, wrdata, mask & 0x83); break;

    case 0x00'01'38:
        if (mask & 0x0000FFFF) RTC_IOWriteHandler(sys, wrdata&0xFFFF,  mask&0xFFFF);
        break;

    case 0x00'01'80: // ipcsync
    {
        if (mask & 0xF00) sys->IPCSyncDataTo9 = (wrdata >> 8) & 0xF;

        if ((wrdata & mask & (1<<13)) && sys->IPCSyncIRQEnableTo9)
            Sched_AddEvent(sys, now + DSClk33(1), Evt_IRQ9_IPCSync);

        if (mask & (1<<14)) sys->IPCSyncIRQEnableTo7 = wrdata & (1<<14);
        break;
    }

    case 0x00'01'84: IPC_FIFOCRWrite(sys, wrdata, mask, now, false); break;
    case 0x00'01'88: IPC_FIFOWrite(sys, wrdata, mask, now, false); break;

    case 0x00'01'A0 ... 0x00'01'B8: GameCard_IOWriteHandler(sys, addr, wrdata, mask, now, false); break;

    case 0x00'01'C0:
        MaskedWrite(sys->SPICR.Raw, wrdata, mask & 0xCF83);

        if (mask & 0xFF0000)
        {
            Sched_AddEvent(sys, now + ((DSClk33(8*8) << sys->SPICR.Baudrate)), Evt_SPI); // checkme: delay?
            switch(sys->SPICR.DeviceSelect)
            {
            case 0: sys->SPIBuf = PMIC_CMDSend(sys, wrdata>>16, sys->SPICR.ChipSelect); break;
            case 1: sys->SPIBuf = Flash_CMDSend(&sys->Firmware, wrdata>>16, sys->SPICR.ChipSelect); break;
            case 2: sys->SPIBuf = TSC_SendCommand(&sys->TSC, wrdata >> 16); break;
            case 3: LogPrint(LOG_ARM7|LOG_UNIMP, "spi RESERVED????????????\n"); break;
            }
            sys->SPICR.Busy = true;
        }
        break;

    case 0x00'02'04: MaskedWrite(sys->ExtMemCR_7.Raw, wrdata, mask & 0x7F); break;

    case 0x00'02'08: MaskedWrite(sys->IME7, wrdata, mask & 1); break;
    case 0x00'02'10: MaskedWrite(sys->IE7, wrdata, mask & 0x01DF3FFF); break;
    case 0x00'02'14: IF7_Clear(sys, wrdata, now); break;

    case 0x00'03'00:
        if (sys->Bios7ProtCur < 0x4000) // can only be written from bios. for... some reason?
        {
            if (mask & 0x1) sys->PostFlag |= wrdata & 0x1;
            if (mask & 0xFF00) // wait control
            {
                switch((wrdata >> 14) & 0x3)
                {
                case 0: // does nothing
                    LogPrint(LOG_ARM7|LOG_ODD, "A7 wrote nothing to HaltCR...?\n");
                    break;
                case 1: // GBA
                    // GBA mode notes:
                    //   DS Lite:
                    //     bits confirmed to impact gba mode:
                    //       pmic - sound amplifier enable
                    //       pmic - lcd backlight enables
                    //       a9 powcr - lcd swap
                    //     dont seem to matter:
                    //       vram banks A/B enable (not sure about border, but it doesn't seem to prevent the game from displaying at least?)
                    //       a9 powcr - 2d engine A enable
                    //       a7 powcr - sound en
                    //       "a9 extmemcnt - main ram gba mode bit"
                    // CHECKME: do you think the dma channel word latches can leak info from nds to gba mode?
                    LogPrint(LOG_ARM7|LOG_UNIMP, "But nobody came...\n\n\n...GBA mode unsupported, sorry!\n");
                    [[fallthrough]];
                case 2: // halt; stop clocking arm7tdmi until IE & IF
                    if (!Console_CheckARM7Wake(sys)) // checkme: might still halt for a little?
                    {
                        sys->A7ClkDisable = true;
                    }
                    break;
                case 3: // sleep
                    // this should be similar to halt but disabling a bunch more hardware...?
                    LogPrint(LOG_ARM7|LOG_UNIMP, "I dont really know what sleep does but it's not in yet!\n");
                    break;
                }
            }
        }
        break;

    case 0x00'03'04: MaskedWrite(sys->PowerCR7.Raw, wrdata, mask & 0x3); break;

    case 0x00'03'08:
        if ((sys->Bios7ProtCur < 0x4000) && (sys->Bios7Prot == 0)) // write once and can probably only be written from bios?
            MaskedWrite(sys->Bios7Prot, wrdata, mask & 0x3FFC); // mask is a guess; in practice the only value ever written is "0x1205"
        break;

    case 0x00'04'00 ... 0x00'04'FC: SoundChannel_IOWrite(sys, addr, wrdata, mask, now); break;

    case 0x00'05'00:
        if (!sys->PowerCR7.AudioPower) break; // read only
        //u16 old = sys->SoundCR.Raw;
        MaskedWrite(sys->SoundCR.Raw, wrdata, mask & 0xBF7F);
        /*if ((wrdata ^ old) & 0x8000) // checkme?
        {
            if (wrdata & 0x8000)
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
        MaskedWrite(sys->SoundBias, wrdata, mask & 0x3FF);
        break;

    case 0x00'05'08:
        if (!sys->PowerCR7.AudioPower) break; // read only
        if (mask & 0x00FF) SoundCapture_CRWrite(sys, wrdata & 0xFF, now, 0);
        if (mask & 0xFF00) SoundCapture_CRWrite(sys, (wrdata >> 8) & 0xFF, now, 1);
        break;

    case 0x00'05'10:
        if (!sys->PowerCR7.AudioPower) break; // read only
        MaskedWrite(sys->SoundCaptures[0].DstAddr, wrdata, mask & 0x07FFFFFC);
        break;
    case 0x00'05'14:
        if (!sys->PowerCR7.AudioPower) break; // read only
        MaskedWrite(sys->SoundCaptures[0].Length, wrdata, mask & 0xFFFF);
        sys->DMA7.Channels[0+DMA7_SoundCapBase].NumWords = sys->SoundCaptures[0].Length + (sys->SoundCaptures[0].Length == 0);
        break;
    case 0x00'05'18:
        if (!sys->PowerCR7.AudioPower) break; // read only
        MaskedWrite(sys->SoundCaptures[1].DstAddr, wrdata, mask & 0x07FFFFFC);
        break;
    case 0x00'05'1C:
        if (!sys->PowerCR7.AudioPower) break; // read only
        MaskedWrite(sys->SoundCaptures[1].Length, wrdata, mask & 0xFFFF);
        sys->DMA7.Channels[1+DMA7_SoundCapBase].NumWords = sys->SoundCaptures[1].Length + (sys->SoundCaptures[1].Length == 0);
        break;


    default:
        LogPrint(LOG_ARM7 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IO7 WRITE: %08"PRIX32" %08"PRIX32" %08"PRIX32" @ %08"PRIX32"\n", addr, wrdata, mask, sys->A7TDMI.ARM.PC);
        break;
    }
    now += DSClk33(1);
    AddBusContention(sys, now, Dev_IO7);
    Bus_TransferPostSetup(sys, 0, false, now, false, cb, man, false);
}

void IO9_Handler(Console* sys, timestamp now)
{
    BusReq* req = &sys->Bus9.PipeFIFO[sys->Bus9.FIFODrainPtr];
    const u32 addr = req->Addr;

    if (req->Write) IO9_Write(sys, addr, now, req->WrVal, MakeWriteMask(addr, req->Size), req->CB, req->Man);
    else            IO9_Read (sys, addr, now, req->CB, req->Man);
}

void IO7_Handler(Console* sys, timestamp now)
{
    BusReq* req = &sys->Bus7.PipeFIFO[sys->Bus7.FIFODrainPtr];
    const u32 addr = req->Addr;

    if (req->Write) IO7_Write(sys, addr, now, req->WrVal, MakeWriteMask(addr, req->Size), req->CB, req->Man);
    else            IO7_Read (sys, addr, now, req->CB, req->Man);
}
