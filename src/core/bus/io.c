#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"



u32 IO7_Read(struct Console* sys, const u32 addr, const u32 mask)
{
    switch(addr & 0xFF'FF'FC)
    {
        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            return DMA_IOReadHandler(sys->DMA7.Channels, addr);

        case 0x00'01'00 ... 0x00'01'0C:
            return Timer_IOReadHandler(sys->IO.Timers7, sys->AHB7.Timestamp, addr);

        case 0x00'01'80: // ipcsync
            return sys->IO.IPCSyncDataTo7
                    | (sys->IO.IPCSyncDataTo9 << 8)
                    | (sys->IO.IPCSyncIRQEnableTo7 << 14);

        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO7 READ: %08lX %08lX\n", addr, mask);
            return 0;
    }
}

void IO7_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    switch(addr & 0xFF'FF'FC)
    {
        // DMA
        /*case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA7_IOWriteHandler(sys, sys->DMA7.Channels, addr, val, mask);
            break;*/

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
        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO7 WRITE: %08lX %08lX %08lX\n", addr, val, mask);
            break;
    }
}

u32 IO9_Read(struct Console* sys, const u32 addr, const u32 mask)
{
    switch (addr & 0xFF'FF'FC)
    {
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

        case 0x00'02'08: // IME
            return sys->IO.IME9;

        case 0x00'02'44:
            // TODO: VRAM CR
            return sys->IO.WRAMCR << 24;

        default:
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO9 READ: %08lX %08lX\n", addr, mask);
            return 0;
    }
}

void IO9_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask)
{
    switch (addr & 0xFF'FF'FC)
    {
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

        case 0x00'02'08: // IME
            sys->IO.IME9 = val & 1 & mask;
            if (mask & 0xFFFF0000) LogPrint(LOG_ALWAYS, "IE9 %08lX %08lX\n", val, mask);
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
            LogPrint(LOG_ARM7 | LOG_UNIMP, "UNIMPLEMENTED IO9 WRITE: %08lX %08lX %08lX @ %08lX\n", addr, val, mask, sys->ARM9.ARM.PC);
            break;
    }
}
