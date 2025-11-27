#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"





// TODO: Testing passes needed
// IPCSYNC
// WRAMCR
// EXMEMCNT

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
            return Timer_IOReadHandler(sys->IO.Timers7, sys->AHB7.Timestamp, addr);

        case 0x00'01'80: // ipcsync
            return sys->IO.IPCSyncDataTo7
                    | (sys->IO.IPCSyncDataTo9 << 8)
                    | (sys->IO.IPCSyncIRQEnableTo7 << 14);

        case 0x00'02'04: // External Memory Control
            return sys->IO.ExtMemCR_Shared.Raw | sys->IO.ExtMemCR_7.Raw;

        case 0x00'02'08: // IME
            return sys->IO.IME7;
        case 0x00'02'10: // IE
            return sys->IO.IE7;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_IF7Update, false);
            return sys->IO.IF7;

        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO7 READ: %08lX %08lX\n", addr, mask);
            return 0;
    }
}

void IO7_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    LogPrint(0, "%08lX %08lX %08lX\n", addr, val, sys->ARM7.ARM.PC);
    switch(addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_Scanline, false);
            //sys->IO.DispStatRW.Raw = val & mask & 0xB8;

            // TODO VCount Match Settings.

            // CHECKME: byte writes probably behave weirdly.
            if ((mask == 0xFF000000) || (mask == 0x00FF0000)) LogPrint(LOG_UNIMP, "UNTESTED: VCOUNT BYTE WRITES!\n");
            if (mask & 0xFFFF0000)
            {
                sys->IO.VCountUpdate = true;
                // CHECKME: how does this mask out?
                // CHECKME: what about > 262 vcounts?
                sys->IO.VCountNew = ((val & mask) >> 16) & 0x1F;
            }
            break;

        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA7_IOWriteHandler(sys, sys->DMA7.Channels, addr, val, mask);
            break;

        case 0x00'01'00 ... 0x00'01'0C:
            Timer_IOWriteHandler(sys->IO.Timers7, sys->AHB7.Timestamp, addr, val, mask);
            break;

        case 0x00'01'80: // ipcsync
        {
            if (mask & 0xF00)
            {
                sys->IO.IPCSyncDataTo9 = (val >> 8) & 0xF;
            }

            if (val & mask & (1<<13))
            {
                // TODO IRQ
            }

            if (mask & (1<<14))
            {
                sys->IO.IPCSyncIRQEnableTo7 = val & (1<<14);
            }
            break;
        }

        case 0x00'02'04: // exmemcnt
            MaskedWrite(sys->IO.ExtMemCR_7.Raw, val, mask & 0x7F);
            break;

        case 0x00'02'08: // IME
            sys->IO.IME7 = val & 1 & mask;
            break;
        case 0x00'02'10: // IE
            sys->IO.IE7 = val & mask & 0x01DF3FFF;
            break;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB7.Timestamp, Sched_IF7Update, false);
            sys->IO.IF7 &= (~val) & mask;
            break;

        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO7 WRITE: %08lX %08lX %08lX\n", addr, val, mask);
            break;
    }
}

u32 IO9_Read(struct Console* sys, const u32 addr, const u32 mask)
{
    switch (addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_Scanline, true);
            return (sys->VCount << 16) | sys->IO.DispStatRO.Raw |  sys->IO.DispStatRW.Raw;

        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            return DMA_IOReadHandler(sys->DMA9.Channels, addr);

        case 0x00'01'00 ... 0x00'01'0C:
            return Timer_IOReadHandler(sys->IO.Timers9, sys->AHB9.Timestamp, addr);

        // IPC
        case 0x00'01'80: // ipcsync
            return sys->IO.IPCSyncDataTo9
                    | (sys->IO.IPCSyncDataTo7 << 8)
                    | (sys->IO.IPCSyncIRQEnableTo9 << 14);

        case 0x00'02'04: // External Memory Control
            return sys->IO.ExtMemCR_Shared.Raw | sys->IO.ExtMemCR_9.Raw;

        case 0x00'02'08: // IME
            return sys->IO.IME9;
        case 0x00'02'10: // IE
            return sys->IO.IE9;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_IF9Update, true);
            return sys->IO.IF9;

        case 0x00'02'44:
            // TODO: VRAM CR
            return sys->IO.WRAMCR << 24;

        default:
            LogPrint(LOG_ARM9 | LOG_UNIMP, "UNIMPLEMENTED IO9 READ: %08lX %08lX\n", addr, mask);
            return 0;
    }
}

void IO9_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    switch (addr & 0xFF'FF'FC)
    {
        case 0x00'00'04:
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_Scanline, true);
            sys->IO.DispStatRW.Raw = val & mask & 0xB8;

            // TODO VCount Match Settings.

            // CHECKME: byte writes probably behave weirdly.
            if ((mask == 0xFF000000) || (mask == 0x00FF0000)) LogPrint(LOG_UNIMP, "UNTESTED: VCOUNT BYTE WRITES!\n");
            if (mask & 0xFFFF0000)
            {
                sys->IO.VCountUpdate = true;
                // CHECKME: how does this mask out?
                // CHECKME: what about > 262 vcounts?
                sys->IO.VCountNew = ((val & mask) >> 16) & 0x1F;
            }
            break;
        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA9_IOWriteHandler(sys, sys->DMA9.Channels, addr, val, mask);
            break;

        case 0x00'01'00 ... 0x00'01'0C:
            Timer_IOWriteHandler(sys->IO.Timers9, sys->AHB9.Timestamp, addr, val, mask);
            break;

        case 0x00'01'80: // ipcsync
        {
            if (mask & 0xF00)
            {
                sys->IO.IPCSyncDataTo7 = (val >> 8) & 0xF;
            }

            if (mask & (1<<13))
            {
                // TODO IRQ
            }

            if (val & mask & (1<<14))
            {
                sys->IO.IPCSyncIRQEnableTo9 = val & (1<<14);
            }
            break;
        }

        case 0x00'02'04: // exmemcnt
            MaskedWrite(sys->IO.ExtMemCR_9.Raw, val, mask & 0x7F);
            bool membit1 = sys->IO.ExtMemCR_Shared.MRSomething1;
            bool membit2 = sys->IO.ExtMemCR_Shared.MRSomething2;

            // TODO: this mask should change with DSi features
            MaskedWrite(sys->IO.ExtMemCR_Shared.Raw, val, mask & 0xE880);

            // these are probably write once...?
            sys->IO.ExtMemCR_Shared.MRSomething1 |= membit1;
            sys->IO.ExtMemCR_Shared.MRSomething2 |= membit2;
            break;

        case 0x00'02'08: // IME
            sys->IO.IME9 = val & 1 & mask;
            break;
        case 0x00'02'10: // IE
            sys->IO.IE9 = val & mask & 0x003F3F7F;
            break;
        case 0x00'02'14: // IF
            Scheduler_RunEventManual(sys, sys->AHB9.Timestamp, Sched_IF9Update, true);
            sys->IO.IF9 &= (~val) & mask;
            break;

        case 0x00'02'44:
        {
            // TODO: VRAM CR
            if (mask & 0xFF000000)
            {
                LogPrint(LOG_ALWAYS, "WRAM %08lX %08lX\n", val, mask);
                sys->IO.WRAMCR = (val >> 24) & 0x3;
            }
            break;
        }


        default:
            LogPrint(LOG_ARM9 | LOG_UNIMP, "UNIMPLEMENTED IO9 WRITE: %08lX %08lX %08lX @ %08lX\n", addr, val, mask, sys->ARM9.ARM.PC);
            break;
    }
}
