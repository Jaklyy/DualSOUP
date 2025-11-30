#include <stdbit.h>
#include "ahb.h"
#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"
#include "io.h"



// distinct nds regions:
// 


/*  io regs that need access size testing: (unaligned accesses too?)
    (all of them kinda do but these ones especially might have weird effects)

    display fifo
    all 3d gpu command ports (fifo included)
*/


// Implementation Notes:

// check each bus in order?

// dbus -> ibus (only for itcm (this is weird on hw?))
// dbus -> ext bus
// ibus -> ext bus
// wbus -> ext bus
// ext bus -> mr bus

// each bus owner has a sort of queue
// 

// bus itself only handles timings and returning/writing data

// bitfield for each bus owner?


/*
    pain and suffering notes:
        itcm data reads seem to cause contention with:
            1. latched instrs (uncached thumb pc & 2)
            2. fully cached instruction reads
            3. itcm instr reads
            note: seems to have no observable impact on active cache streaming and uncached reads?
            all one cycle instr fetches seem to behave identically?
            TODO: test aborted accesses; data aborts shouldn't matter, but prefetch aborts might.
*/

bool AHB9_SyncWith7GTE(struct Console* sys)
{
    if (sys->AHB9.Timestamp >= Console_GetARM7Cur(sys))
    {
        CR_Switch(sys->HandleARM7);
        return false;
    }
    else return true;
}

bool AHB9_SyncWith7GT(struct Console* sys)
{
    if (sys->AHB9.Timestamp > Console_GetARM7Cur(sys))
    {
        CR_Switch(sys->HandleARM7);
        return false;
    }
    else return true;
}

bool AHB7_SyncWith9GTE(struct Console* sys)
{
    if (sys->AHB7.Timestamp >= Console_GetARM9Cur(sys))
    {
        CR_Switch(sys->HandleARM9);
        return false;
    }
    else return true;
}

bool AHB7_SyncWith9GT(struct Console* sys)
{
    if (sys->AHB7.Timestamp > Console_GetARM9Cur(sys))
    {
        CR_Switch(sys->HandleARM9);
        return false;
    }
    else return true;
}

void Timing16(struct AHB* bus, const u32 mask)
{
    bus->Timestamp += ((mask == u32_max) ? 2 : 1);
}

void Timing32(struct AHB* bus)
{
    bus->Timestamp += 1;
}

void WriteContention(struct AHB* bus, const u8 device, const bool seq)
{
    // check if the device we're accessing is busy
    // sequential accesses shouldn't need to be checked on
    if (!seq && (bus->BusyDevice == device) && (bus->Timestamp < bus->BusyDeviceTS))
    {
        bus->Timestamp = bus->BusyDeviceTS;
    }
}

static inline void AddWriteContention(struct AHB* bus, const u8 device)
{
    bus->BusyDevice = device;
    bus->BusyDeviceTS = bus->Timestamp+1;
}

#define VRAMRET(x) \
    any = true; \
    if (timings) \
    { \
        WriteContention(&sys->AHB9, Dev_##x , seq); \
        Timing16(&sys->AHB9, mask); \
    } \
    if (write) \
    { \
        if (timings) \
            AddWriteContention(&sys->AHB9, Dev_##x ); \
        MemoryWrite(32, sys-> x , addr, x##_Size, val, mask); \
    } \
    else /* read */ \
    { \
        ret = MemoryRead(32, sys-> x , addr, x##_Size); \
    } \

u32 VRAM_LCD(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    bool any = false;
    u32 ret = 0;
    switch((addr >> 12) & 0xFC)
    {
        case 0x00 ... 0x1C:
            if ((sys->VRAMCR[0].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_A)
            }
            break;

        case 0x20 ... 0x3C:
            if ((sys->VRAMCR[1].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_B)
            }
            break;

        case 0x40 ... 0x5C:
            if ((sys->VRAMCR[2].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_C)
            }
            break;

        case 0x60 ... 0x7C:
            if ((sys->VRAMCR[3].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_D)
            }
            break;

        case 0x80 ... 0x8C:
            if ((sys->VRAMCR[4].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_E)
            }
            break;

        case 0x90:
            if ((sys->VRAMCR[5].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_F)
            }
            break;

        case 0x94:
            if ((sys->VRAMCR[6].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_G)
            }
            break;

        case 0x98 ... 0x9C:
            if ((sys->VRAMCR[7].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_H)
            }
            break;

        case 0xA0:
            if ((sys->VRAMCR[8].Raw & 0x87) == 0x80)
            {
                VRAMRET(VRAM_I)
            }
            break;

        default:
            break;
    }
    if (!any)
    {
        if (timings)
        {
            LogPrint(LOG_ARM9|LOG_ODD|LOG_VRAM, "UNMAPPED LCD VRAM ACCESS? %08X %08X\n", val, addr);
            Timing32(&sys->AHB9);
        }
    }
    return ret;
}

#undef VRAMRET

#define VRAMRET(x) \
    if (base == index) \
    { \
        any = true; \
        if (timings) \
        { \
            WriteContention(&sys->AHB9, Dev_##x , seq); \
            Timing16(&sys->AHB9, mask); \
        } \
        if (write) \
        { \
            if (timings) \
                AddWriteContention(&sys->AHB9, Dev_##x ); \
            MemoryWrite(32, sys-> x , addr, x##_Size, val, mask); \
        } \
        else /* read */ \
        { \
            ret |= MemoryRead(32, sys-> x , addr, x##_Size); \
        } \
    }

u32 VRAM_BGA(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    u32 ret = 0;
    bool any = false;
    if ((sys->VRAMCR[0].Raw & 0x87) == 0x81)
    {
        u32 base = (sys->VRAMCR[0].Offset * 0x20000);
        u32 index = addr & 0x70000;
        VRAMRET(VRAM_A)
    }
    if ((sys->VRAMCR[1].Raw & 0x87) == 0x81)
    {
        u32 base = (sys->VRAMCR[1].Offset * 0x20000);
        u32 index = addr & 0x70000;
        VRAMRET(VRAM_B)
    }
    if ((sys->VRAMCR[2].Raw & 0x87) == 0x81)
    {
        u32 base = (sys->VRAMCR[2].Offset * 0x20000);
        u32 index = addr & 0x70000;
        VRAMRET(VRAM_C)
    }
    if ((sys->VRAMCR[3].Raw & 0x87) == 0x81)
    {
        u32 base = (sys->VRAMCR[3].Offset * 0x20000);
        u32 index = addr & 0x70000;
        VRAMRET(VRAM_D)
    }
    if ((sys->VRAMCR[4].Raw & 0x87) == 0x81)
    {
        u32 base = 0;
        u32 index = addr & 0x78000;
        VRAMRET(VRAM_E)
    }
    if ((sys->VRAMCR[5].Raw & 0x87) == 0x81)
    {
        u32 base = (((sys->VRAMCR[5].Offset & 1) * 0x4000) + ((sys->VRAMCR[5].Offset >> 1) * 0x10000));
        u32 index = addr & 0x7E000;
        VRAMRET(VRAM_F)
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x81)
    {
        u32 base = (((sys->VRAMCR[6].Offset & 1) * 0x4000) + ((sys->VRAMCR[6].Offset >> 1) * 0x10000));
        u32 index = addr & 0x7E000;
        VRAMRET(VRAM_G)
    }
    if (!any)
    {
        if (timings)
        {
            LogPrint(LOG_ARM9|LOG_ODD|LOG_VRAM, "UNMAPPED BG A VRAM ACCESS? %08X %08X\n", val, addr);
            Timing32(&sys->AHB9);
        }
    }
    return ret;
}

u32 VRAM_OBJA(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    u32 ret = 0;
    bool any = false;
    if ((sys->VRAMCR[0].Raw & 0x87) == 0x82)
    {
        u32 base = (sys->VRAMCR[0].Offset * 0x20000);
        u32 index = addr & 0x30000;
        VRAMRET(VRAM_A)
    }
    if ((sys->VRAMCR[1].Raw & 0x87) == 0x82)
    {
        u32 base = (sys->VRAMCR[1].Offset * 0x20000);
        u32 index = addr & 0x30000;
        VRAMRET(VRAM_B)
    }
    if ((sys->VRAMCR[4].Raw & 0x87) == 0x82)
    {
        u32 base = 0;
        u32 index = addr & 0x38000;
        VRAMRET(VRAM_E)
    }
    if ((sys->VRAMCR[5].Raw & 0x87) == 0x82)
    {
        u32 base = (((sys->VRAMCR[5].Offset & 1) * 0x4000) + ((sys->VRAMCR[5].Offset >> 1) * 0x10000));
        u32 index = addr & 0x3E000;
        VRAMRET(VRAM_F)
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x82)
    {
        u32 base = (((sys->VRAMCR[6].Offset & 1) * 0x4000) + ((sys->VRAMCR[6].Offset >> 1) * 0x10000));
        u32 index = addr & 0x3E000;
        VRAMRET(VRAM_G)
    }
    if (!any)
    {
        if (timings)
        {
            LogPrint(LOG_ARM9|LOG_ODD|LOG_VRAM, "UNMAPPED OBJ A VRAM ACCESS? %08X %08X\n", val, addr);
            Timing32(&sys->AHB9);
        }
    }
    return ret;
}

u32 VRAM_BGB(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    u32 ret = 0;
    bool any = false;
    if ((sys->VRAMCR[2].Raw & 0x87) == 0x84)
    {
        u32 base = 0;
        u32 index = addr & 0x10000;
        VRAMRET(VRAM_C)
    }
    if ((sys->VRAMCR[7].Raw & 0x87) == 0x81)
    {
        u32 base = 0;
        u32 index = addr & 0x1C000;
        VRAMRET(VRAM_H)
    }
    if ((sys->VRAMCR[8].Raw & 0x87) == 0x81)
    {
        u32 base = 0x8000;
        u32 index = addr & 0x1E000;
        VRAMRET(VRAM_I)
    }
    if (!any)
    {
        if (timings)
        {
            LogPrint(LOG_ARM9|LOG_ODD|LOG_VRAM, "UNMAPPED BG B VRAM ACCESS? %08X %08X\n", val, addr);
            Timing32(&sys->AHB9);
        }
    }
    return ret;
}

u32 VRAM_OBJB(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    u32 ret = 0;
    bool any = false;
    if ((sys->VRAMCR[3].Raw & 0x87) == 0x84)
    {
        u32 base = 0;
        u32 index = addr & 0x10000;
        VRAMRET(VRAM_C)
    }
    if ((sys->VRAMCR[8].Raw & 0x87) == 0x82)
    {
        u32 base = 0;
        u32 index = addr & 0x1E000;
        VRAMRET(VRAM_I)
    }
    if (!any)
    {
        if (timings)
        {
            LogPrint(LOG_ARM9|LOG_ODD|LOG_VRAM, "UNMAPPED OBJ B VRAM ACCESS? %08X %08X\n", val, addr);
            Timing32(&sys->AHB9);
        }
    }
    return ret;
}

u32 VRAM_ARM7(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    u32 ret = 0;
    bool any = false;
    if ((sys->VRAMCR[2].Raw & 0x87) == 0x82)
    {
        u32 base = (sys->VRAMCR[2].Offset * 0x20000);
        u32 index = addr & 0x10000;
        VRAMRET(VRAM_C)
    }
    if ((sys->VRAMCR[3].Raw & 0x87) == 0x82)
    {
        u32 base = (sys->VRAMCR[3].Offset * 0x20000);
        u32 index = addr & 0x10000;
        VRAMRET(VRAM_D)
    }
    if (!any)
    {
        if (timings)
        {
            LogPrint(LOG_ARM9|LOG_ODD|LOG_VRAM, "UNMAPPED ARM7 VRAM ACCESS? %08X %08X\n", val, addr);
            Timing32(&sys->AHB9);
        }
    }
    return ret;
}

#undef VRAMRET

u32 VRAM_ARM9(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings)
{
    switch((addr >> 20) & 0xE)
    {
        case 0:  return VRAM_BGA(sys, addr, mask, seq, write, val, timings);
        case 2:  return VRAM_BGB(sys, addr, mask, seq, write, val, timings);
        case 4:  return VRAM_OBJA(sys, addr, mask, seq, write, val, timings);
        case 6:  return VRAM_OBJB(sys, addr, mask, seq, write, val, timings);
        default: return VRAM_LCD(sys, addr, mask, seq, write, val, timings);
    }
}

// branches = ns
// all others = s

// thumb alternates between fetching a halfword and reading a latched halfword when doing uncached bus accesses
// above needs more testing maybe? how does it work with cache streaming exactly?

// first we need to check for cache streaming being active
// if we're doing a nonsequential code fetch we need to stall until it's over
// if we're doing a sequential code fetch we need to stall until the next access is ready

// Welcome to my special little hell. :D
u32 Bus_MainRAM_Read(struct Console* sys, struct AHB* buscur, const bool bus9, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq, const bool timings)
{
    struct BusMainRAM* busmr = &sys->BusMR;

    if (timings)
    {
        // main ram cannot handle a read and write burst at the same time.
        if (*seq && !busmr->LastWasRead) *seq = false;
        busmr->LastWasRead = true;

        // there are two known conditions where a burst will be forcibly restarted:
        // if a burst lasts longer than 241 cycles
        // if a burst that started in the last 6 bytes of a 32 byte chunk crosses the 32 byte boundary
        // CHECKME: does this cause the arm9/arm7 bus owner to have to renegotiate permissions?
        // CHECKME: does the burst restart immediately or when the owner next attempts an access? (this matters for dma)
        if (*seq && (((buscur->Timestamp - busmr->BurstStartTS) > 241) || (busmr->WeirdStartAddr && ((addr & 0x1E) == 0))))
        {
            *seq = false;
        }

        // if main ram is still busy the accessing bus needs to wait until it's available to begin a new burst to it
        if (!*seq && (buscur->Timestamp < busmr->BusyTS))
        {
            buscur->Timestamp = busmr->BusyTS;
        }

        // if there is even the slightest chance that the other bus can access main ram before us we need to catch it up.
        // Note: Main RAM respects atomic accesses.
        // CHECKME: i think this logic works?????
        while(true)
        {
            if (atomic) break;

            busmr->LastWasARM9 = bus9;

            if (sys->ExtMemCR_Shared.MRPriority)
            {
                // arm7 has priority
                if (bus9) AHB9_SyncWith7GTE(sys);
                else      AHB7_SyncWith9GT (sys);
            }
            else
            {
                // arm9 has priority
                if (bus9) AHB9_SyncWith7GT (sys);
                else      AHB7_SyncWith9GTE(sys);
            }

            // if main ram is still busy the accessing bus needs to wait until it's available to begin a new burst to it
            if ((busmr->LastWasARM9 != bus9) && (buscur->Timestamp < busmr->BusyTS))
            {
                *seq = false;
                buscur->Timestamp = busmr->BusyTS;
            }
            else
            {
                break;
            }
        }

        // ok we actually have confirmed bus permission now!!! yipee!!!

        if (!*seq) // nonsequential
        {
            busmr->BurstStartTS = buscur->Timestamp;

            // if the burst starts in the last 6 bytes of a 32 byte chunk then the burst will restart if it crosses the 32 byte boundary
            busmr->WeirdStartAddr = ((addr & 0x1E) >= 0x1A);

            if (mask == u32_max)
            {
                buscur->Timestamp += 5;
            }
            else // 8 / 16
            {
                buscur->Timestamp += 4;
            }
        }
        else // sequential
        {
            if (mask == u32_max)
            {
                // this takes 2 cycles, but it can appear to take 1 cycle under certain situations,
                // due to main ram having the ability to prefetch slightly ahead if the burst is held but not immediately read from. (identified with 32 bit dma)

                buscur->Timestamp += 1;

                if (hold && buscur->Timestamp < (busmr->LastAccessTS + 2))
                    buscur->Timestamp = (busmr->LastAccessTS + 2);
            }
            else // 8 / 16
            {
                // always 1 cycle
                buscur->Timestamp += 1;
            }
        }
        if (hold) busmr->LastAccessTS = buscur->Timestamp;
        busmr->BusyTS = buscur->Timestamp + 3;
    }

    return MemoryRead(32, sys->MainRAM, addr, MainRAM_Size);
}

void Bus_MainRAM_Write(struct Console* sys, struct AHB* buscur, const bool bus9, u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq, const bool timings)
{
    struct BusMainRAM* busmr = &sys->BusMR;

    if (timings)
    {
        // main ram cannot handle a read and write burst at the same time.
        if (*seq && busmr->LastWasRead) *seq = false;
        busmr->LastWasRead = false;

        // there are two known conditions where a burst will be forcibly restarted:
        // if a burst lasts longer than 241 cycles
        // if a burst that started in the last 6 bytes of a 32 byte chunk crosses the 32 byte boundary
        // CHECKME: does this cause the arm9/arm7 bus owner to have to renegotiate permissions?
        // CHECKME: does the burst restart immediately or when the owner next attempts an access? (this matters for dma)
        // CHECKME: confirm writes too?
        if (*seq && (((buscur->Timestamp - busmr->BurstStartTS) > 241) || (busmr->WeirdStartAddr && ((addr & 0x1E) == 0))))
        {
            *seq = false;
        }

        // if main ram is still busy the accessing bus needs to wait until it's available to begin a new burst to it
        if (!*seq && (buscur->Timestamp < busmr->BusyTS))
        {
            buscur->Timestamp = busmr->BusyTS;
        }

        // if there is even the slightest chance that the other bus can access main ram before us we need to catch it up.
        // Note: Main RAM respects atomic accesses.
        // CHECKME: i think this logic works?????
        while(true)
        {
            if (atomic) break;

            busmr->LastWasARM9 = bus9;

            if (sys->ExtMemCR_Shared.MRPriority)
            {
                // arm7 has priority
                if (bus9) AHB9_SyncWith7GTE(sys);
                else       AHB7_SyncWith9GT (sys);
            }
            else
            {
                // arm9 has priority
                if (bus9) AHB9_SyncWith7GT (sys);
                else      AHB7_SyncWith9GTE(sys);
            }

            // if main ram is still busy the accessing bus needs to wait until it's available to begin a new burst to it
            if ((busmr->LastWasARM9 != bus9) && (buscur->Timestamp < busmr->BusyTS))
            {
                *seq = false;
                buscur->Timestamp = busmr->BusyTS;
            }
            else
            {
                break;
            }
        }

        // ok we actually have confirmed bus permission now!!! yipee!!!

        if (!*seq) // nonsequential
        {
            busmr->BurstStartTS = buscur->Timestamp;

            // if the burst starts in the last 6 bytes of a 32 byte chunk then the burst will restart if it crosses the 32 byte boundary
            busmr->WeirdStartAddr = ((addr & 0x1E) >= 0x1A);

            if (stdc_count_ones(mask) == 16)
            {
                buscur->Timestamp += 2;
            }
            else // 8 / 32
            {
                buscur->Timestamp += 3;
            }
        }
        else // sequential
        {
            if (stdc_count_ones(mask) == 32)
            {
                buscur->Timestamp += 2;
            }
            else // 8 / 16
            {
                buscur->Timestamp += 1;
            }
        }
        busmr->BusyTS = buscur->Timestamp + ((stdc_count_ones(mask) == 16) ? 6 : 5);
    }

    MemoryWrite(32, sys->MainRAM, addr, MainRAM_Size, val, mask);
}

u32 AHB9_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq, const bool timings)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, does that also apply to arm9?
    // if so, is the alignment properly enforced by all bus devices?

    if (timings)
    {
        if (sys->AHB9.Timestamp < *ts)
            sys->AHB9.Timestamp = *ts;
    }

    addr &= ~3; // 4 byte aligned value used to simplify read logic.

    const unsigned width = stdc_count_ones(mask);
    u32 ret;

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        ret = Bus_MainRAM_Read(sys, &sys->AHB9, true, addr, mask, atomic, hold, seq, timings);
        break;

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        if (timings)
        {
            WriteContention(&sys->AHB9, Dev_WRAM, *seq);
            Timing32(&sys->AHB9);
        }
        switch(sys->WRAMCR)
        {
            case 0:
                ret = sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size)-1)))/4]; break;
            case 1:
                ret = sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)) + (SharedWRAM_Size/2))/4]; break;
            case 2:
                ret = sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)))/4]; break;
            case 3:
                ret = 0; break;
        }
        break;

    case 0x04: // Memory Mapped IO
        if (timings)
        {
            WriteContention(&sys->AHB9, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
            Timing32(&sys->AHB9); // checkme: does all of IO have the exact same timings?
        }
        ret = IO9_Read(sys, addr, mask);
        break;

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerControl9.PPUBPower : sys->PowerControl9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE WRITE?\n");
            if (timings)
            {
                Timing32(&sys->AHB9);
            }
            ret = 0;
        }
        else
        {
            if (timings)
            {
                WriteContention(&sys->AHB9, Dev_Palette, *seq);
                Timing16(&sys->AHB9, mask);
            }
            ret = MemoryRead(32, sys->Palette, addr, Palette_Size);
        }
        break;

    case 0x06: // VRAM
        // TODO: 2d gpu contention timings
        ret = VRAM_ARM9(sys, addr, mask, *seq, false, 0, true);
        break;

    case 0x07: // 2D GPU OAM
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerControl9.PPUBPower : sys->PowerControl9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED OAM WRITE?\n");
            if (timings)
            {
                Timing32(&sys->AHB9);
            }
            ret = 0;
        }
        else
        {
            if (timings)
            {
                WriteContention(&sys->AHB9, Dev_Palette, *seq);
                Timing32(&sys->AHB9);
            }
            ret = MemoryRead(32, sys->OAM, addr, OAM_Size);
        }
        break;

    case 0x08 ... 0x09: // GBA Cartridge ROM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented READ%i: GBAROM\n", width);
        ret = 0; // TODO
        break;

    case 0x0A: // GBA Cartridge RAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented READ%i: GBARAM\n", width);
        ret = 0; // TODO
        break;

    case 0xFF: // NDS BIOS
        if ((addr & 0xFFFFF000) == 0xFFFF0000)
        {
            // bios does not have contention, interestingly enough.
            if (timings)
            {
                Timing32(&sys->AHB9);
            }
            ret = MemoryRead(32, sys->NTRBios9, addr, NTRBios9_Size);
            break;
        }
        else [[fallthrough]];

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %i bit read from unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        if (timings)
        {
            Timing32(&sys->AHB9);
        }
        ret = 0; // always reads 0
        break;
    }

    if (timings)
    {
        *ts = sys->AHB9.Timestamp;
    }
    return ret;
}

void AHB9_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq, const bool timings)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, does that also apply to arm9?
    // if so, is the alignment properly enforced by all bus devices?

    if (timings)
    {
        if (sys->AHB9.Timestamp < *ts)
            sys->AHB9.Timestamp = *ts;
    }

    addr &= ~3; // 4 byte aligned value used to simplify read logic.

    const unsigned width = stdc_count_ones(mask);

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        if (addr < 0x02000100) LogPrint(LOG_ALWAYS, "MRLOG9: %i %08X\n", val, val);
        Bus_MainRAM_Write(sys, &sys->AHB9, true, addr, val, mask, atomic, seq, timings);
        break;

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        if (timings)
        {
            WriteContention(&sys->AHB9, Dev_WRAM, *seq);
            Timing32(&sys->AHB9);
            AddWriteContention(&sys->AHB9, Dev_WRAM);
        }
        switch(sys->WRAMCR)
        {
            case 0:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size)-1)))/4], val, mask); break;
            case 1:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)) + (SharedWRAM_Size/2))/4], val, mask); break;
            case 2:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)))/4], val, mask); break;
            case 3:
                break;
        }
        break;

    case 0x04: // Memory Mapped IO
        if (timings)
        {
            WriteContention(&sys->AHB9, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
            Timing32(&sys->AHB9); // checkme: does all of IO have the exact same timings?
            AddWriteContention(&sys->AHB9, Dev_IO);
        }
        IO9_Write(sys, addr, val, mask);
        break;

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerControl9.PPUBPower : sys->PowerControl9.PPUAPower) || (width == 8))
        {
            (width == 8) ? LogPrint(LOG_ARM9|LOG_ODD, "8 BIT PALETTE WRITE?\n") : LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE WRITE?\n");
            if (timings)
            {
                // CHECKME: contention for bytes?
                Timing32(&sys->AHB9);
            }
        }
        else
        {
            if (timings)
            {
                WriteContention(&sys->AHB9, Dev_Palette, *seq);
                Timing16(&sys->AHB9, mask);
                AddWriteContention(&sys->AHB9, Dev_Palette);
            }
            MemoryWrite(32, sys->Palette, addr, Palette_Size, val, mask);
        }
        break;

    case 0x06: // VRAM
        // TODO: 2d gpu contention timings
        if (width == 8)
        {
            LogPrint(LOG_ARM9|LOG_ODD, "ARM9: 8 BIT VRAM WRITE?\n");
            if (timings)
            {
                // CHECKME: contention for bytes?
                Timing32(&sys->AHB9);
            }
        }
        else
        {
            VRAM_ARM9(sys, addr, mask, *seq, true, val, true);
        }
        break;

    case 0x07: // 2D GPU OAM
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerControl9.PPUBPower : sys->PowerControl9.PPUAPower) || (width == 8))
        {
            (width == 8) ? LogPrint(LOG_ARM9|LOG_ODD, "8 BIT OAM WRITE?\n") : LogPrint(LOG_ARM9|LOG_ODD, "DISABLED OAM WRITE?\n");
            if (timings)
            {
                // CHECKME: contention for bytes?
                Timing32(&sys->AHB9);
            }
        }
        else
        {
            if (timings)
            {
                WriteContention(&sys->AHB9, Dev_OAM, *seq);
                Timing32(&sys->AHB9);
                AddWriteContention(&sys->AHB9, Dev_OAM);
            }
            MemoryWrite(32, sys->OAM, addr, OAM_Size, val, mask);
        }
        break;

    case 0x08 ... 0x09: // GBA Cartridge ROM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented WRITE%i: GBAROM\n", width);
        // TODO
        break;

    case 0x0A: // GBA Cartridge RAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented WRITE%i: GBARAM\n", width);
        // TODO
        break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %i bit write to unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        if (timings)
        {
            Timing32(&sys->AHB9);
        }
        // always reads 0
        break;
    }

    if (timings)
    {
        *ts = sys->AHB9.Timestamp;
    }
}

// dma
// 4 sub buses (probably not how this is actually handled?)
// priority lowest id to highest
// - dma0
// - dma1
// - dma2
// - dma3

// arm9
// 3 sub buses
// equal-ish priority
// - arm9 instruction
// - arm9 data
// - arm9 write buffer

bool AHB9_NegOwnership(struct Console* sys, timestamp* cur, const u8 priority, const bool atomic)
{
    struct AHB* bus = &sys->AHB9;

    // ensure component is caught up to bus
    if (*cur < sys->AHB9.Timestamp) *cur = sys->AHB9.Timestamp;

    // check if anything else is able to run
    u8 id = priority;
    if (!atomic && (*cur >= DMA_CheckNext(sys, &sys->DMA9, &id)))
    {
        DMA_Run(sys, &sys->DMA9, id, true);

        // catch up component to bus again; otherwise catch bus up to component
        if (*cur < sys->AHB9.Timestamp) *cur = sys->AHB9.Timestamp;
        else sys->AHB9.Timestamp = *cur;

        return false;
    }
    if (sys->AHB9.Timestamp < *cur) sys->AHB9.Timestamp = *cur;

    return true;
}

u32 AHB7_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq, const bool timings)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, this presumably still applies to arm9.
    // is the alignment properly enforced by all bus devices?
    if (timings)
    {
        if (*ts < sys->AHB7.Timestamp)
            *ts = sys->AHB7.Timestamp;
    }

    addr &= ~3; // 4 byte aligned value used to simplify read logic.

    const unsigned width = stdc_count_ones(mask);
    u32 ret;

    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x000: // NDS BIOS
        // TODO: Bios protection.
        if (timings)
        {
            // CHECKME: does bios7 write contention work weirdly with bios prot?
            WriteContention(&sys->AHB7, Dev_Bios7, *seq);
            Timing32(&sys->AHB7);
        }
        ret = MemoryRead(32, sys->NTRBios7, addr, NTRBios7_Size);
        break;

    case 0x020: // Main RAM
    case 0x028:
        ret = Bus_MainRAM_Read(sys, &sys->AHB7, false, addr, mask, atomic, hold, seq, timings);
        break;

    case 0x030: // Shared WRAM
        if (timings)
        {
            WriteContention(&sys->AHB7, Dev_WRAM, *seq);
            Timing32(&sys->AHB7);
        }
        switch(sys->WRAMCR)
        {
            case 0:
                ret = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size); break;
            case 1:
                ret = sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)))/4]; break;
            case 2:
                ret = sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)) + (SharedWRAM_Size/2))/4]; break;
            case 3:
                ret = sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size)-1)))/4]; break;
        }
        break;

    case 0x038: // ARM7 WRAM
        if (timings)
        {
            WriteContention(&sys->AHB7, Dev_A7WRAM, *seq);
            Timing32(&sys->AHB7);
        }
        ret = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size);
        break;

    case 0x040: // Memory Mapped IO
        if (timings)
        {
            WriteContention(&sys->AHB7, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
            Timing32(&sys->AHB7); // checkme: does all of IO have the exact same timings?
        }
        ret = IO7_Read(sys, addr, mask);
        break;

    case 0x060: // VRAM
    case 0x068: // VRAM
        ret = VRAM_ARM7(sys, addr, mask, *seq, false, 0, true);
        break;

    case 0x080 ... 0x098: // GBA Cartridge ROM
        LogPrint(LOG_UNIMP|LOG_ARM7, "NTR_AHB7: Unimplemented READ%i: GBAROM\n", width);
        ret = 0; // TODO
        break;

    case 0x0A0: // GBA Cartridge RAM
    case 0x0A8: // GBA Cartridge RAM
        LogPrint(LOG_UNIMP|LOG_ARM7, "NTR_AHB7: Unimplemented READ%i: GBARAM\n", width);
        ret = 0; // TODO
        break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM7,"NTR_AHB7: %i bit read from unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        if (timings)
        {
            Timing32(&sys->AHB7);
        }
        ret = 0; // always reads 0
        break;
    }

    if (timings)
    {
        *ts = sys->AHB7.Timestamp;
    }
    return ret;
}

void AHB7_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq, const bool timings)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, this presumably still applies to arm9.
    // is the alignment properly enforced by all bus devices?
    if (timings)
    {
        if (*ts < sys->AHB7.Timestamp)
            *ts = sys->AHB7.Timestamp;
    }

    addr &= ~3; // 4 byte aligned value used to simplify write logic.

    const unsigned width = stdc_count_ones(mask);

    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x000: // NDS BIOS
        if (timings)
        {
            // CHECKME: does bios7 write contention work weirdly with bios prot?
            WriteContention(&sys->AHB7, Dev_Bios7, *seq);
            Timing32(&sys->AHB7);
            AddWriteContention(&sys->AHB7, Dev_WRAM);
        }
        break;

    case 0x020: // Main RAM
    case 0x028: // Main RAM
        if (addr < 0x02000100) LogPrint(LOG_ALWAYS, "MRLOG7: %i %08X\n", val, val);
        Bus_MainRAM_Write(sys, &sys->AHB7, false, addr, val, mask, atomic, seq, timings);
        break;

    case 0x030: // Shared WRAM
        if (timings)
        {
            WriteContention(&sys->AHB7, Dev_WRAM, *seq);
            Timing32(&sys->AHB7);
            AddWriteContention(&sys->AHB7, Dev_WRAM);
        }
        switch(sys->WRAMCR)
        {
            case 0:
                MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, val, mask); break;
            case 1:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)))/4], val, mask); break;
            case 2:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)) + (SharedWRAM_Size/2))/4], val, mask); break;
            case 3:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size)-1)))/4], val, mask); break;
        }
        break;

    case 0x038: // ARM7 WRAM
        if (timings)
        {
            WriteContention(&sys->AHB7, Dev_WRAM, *seq);
            Timing32(&sys->AHB7);
            AddWriteContention(&sys->AHB7, Dev_WRAM);
        }
        MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, val, mask);
        break;

    case 0x040: // Memory Mapped IO
        if (timings)
        {
            WriteContention(&sys->AHB7, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
            Timing32(&sys->AHB7); // checkme: does all of IO have the exact same timings?
            AddWriteContention(&sys->AHB7, Dev_IO);
        }
        IO7_Write(sys, addr, val, mask);
        break;
    case 0x048: // 

    case 0x060: // VRAM
    case 0x068: // VRAM
        VRAM_ARM7(sys, addr, mask, *seq, true, val, true);
        break;

    case 0x080 ... 0x098: // GBA Cartridge ROM
        LogPrint(LOG_UNIMP|LOG_ARM7, "NTR_AHB7: Unimplemented WRITE%i: GBAROM\n", width);
        // TODO
        break;

    case 0x0A0: // GBA Cartridge RAM
    case 0x0A8: // GBA Cartridge RAM
        LogPrint(LOG_UNIMP|LOG_ARM7, "NTR_AHB7: Unimplemented WRITE%i: GBARAM\n", width);
        // TODO
        break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM7,"NTR_AHB7: %i bit write to unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        if (timings)
        {
            Timing32(&sys->AHB7);
        }
        break;
    }

    if (timings)
    {
        *ts = sys->AHB7.Timestamp;
    }
}
