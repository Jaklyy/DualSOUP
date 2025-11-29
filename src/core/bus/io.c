#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"





// TODO: Regs im 99% confident about:
// Timers

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
            return sys->IPCSyncDataTo7
                    | (sys->IPCSyncDataTo9 << 8)
                    | (sys->IPCSyncIRQEnableTo7 << 14);

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
            if (mask & 0xF00)
            {
                sys->IPCSyncDataTo9 = (val >> 8) & 0xF;
            }

            if (val & mask & (1<<13))
            {
                // TODO IRQ
            }

            if (mask & (1<<14))
            {
                sys->IPCSyncIRQEnableTo7 = val & (1<<14);
            }
            break;
        }

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
            return sys->IPCSyncDataTo9
                    | (sys->IPCSyncDataTo7 << 8)
                    | (sys->IPCSyncIRQEnableTo9 << 14);

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

        case 0x00'03'04:
            return sys->PowerControl9.Raw;

        case 0x00'10'00:
            return sys->PPU_B.DisplayCR.Raw;

        case 0x00'10'08:
            return sys->PPU_B.BGCR[0].Raw | (sys->PPU_B.BGCR[1].Raw << 16);
            break;
        case 0x00'10'0C:
            return sys->PPU_B.BGCR[2].Raw | (sys->PPU_B.BGCR[3].Raw << 16);
            break;

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
            if (mask & 0xF00)
            {
                sys->IPCSyncDataTo7 = (val >> 8) & 0xF;
            }

            if (mask & (1<<13))
            {
                // TODO IRQ
            }

            if (val & mask & (1<<14))
            {
                sys->IPCSyncIRQEnableTo9 = val & (1<<14);
            }
            break;
        }

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
