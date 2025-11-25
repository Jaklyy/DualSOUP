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

// branches = ns
// all others = s

// thumb alternates between fetching a halfword and reading a latched halfword when doing uncached bus accesses
// above needs more testing maybe? how does it work with cache streaming exactly?

// first we need to check for cache streaming being active
// if we're doing a nonsequential code fetch we need to stall until it's over
// if we're doing a sequential code fetch we need to stall until the next access is ready

// Welcome to my special little hell. :D
u32 Bus_MainRAM_Read(struct Console* sys, struct AHB* buscur, const timestamp MainRAMThreatTS, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq)
{
    struct BusMainRAM* busmr = &sys->BusMR;

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
    if (!atomic && (buscur->Timestamp < MainRAMThreatTS))
    {
        // TODO: run arm7 junk
        if (false) // arm7 ran
        {
            *seq = false;
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

            if (buscur->Timestamp < (busmr->LastAccessTS + 2))
                buscur->Timestamp = (busmr->LastAccessTS + 2);
        }
        else // 8 / 16
        {
            // always 1 cycle
            buscur->Timestamp += 1;
        }
    }
    busmr->LastAccessTS = buscur->Timestamp;
    busmr->BusyTS = buscur->Timestamp + 3;

    return MemoryRead(32, sys->MainRAM, addr, MainRAM_Size);
}

void Bus_MainRAM_Write(struct Console* sys, struct AHB* buscur, const timestamp MainRAMThreatTS, u32 addr, const u32 val, const u32 mask, const bool atomic, const bool hold, bool* seq)
{
    struct BusMainRAM* busmr = &sys->BusMR;

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
    if (!atomic && (buscur->Timestamp < MainRAMThreatTS))
    {
        // TODO: run arm7 junk
        if (false) // arm7 ran
        {
            *seq = false;
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
        if (stdc_count_ones(mask) == 16)
        {
            // this takes 2 cycles, but it can appear to take 1 cycle under certain situations,
            // due to main ram having the ability to prefetch slightly ahead if the burst is held but not immediately read from. (identified with 32 bit dma)

            buscur->Timestamp += 1;
        }
        else // 8 / 16
        {
            // always 1 cycle
            buscur->Timestamp += 1;
        }
    }
    busmr->BusyTS = buscur->Timestamp + ((stdc_count_ones(mask) == 16) ? 6 : 5);

    MemoryWrite(32, sys->MainRAM, addr, MainRAM_Size, val, mask);
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

inline void AddWriteContention(struct AHB* bus, const u8 device)
{
    bus->BusyDevice = device;
    bus->BusyDeviceTS = bus->Timestamp+1;
}

u32 AHB9_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, does that also apply to arm9?
    // if so, is the alignment properly enforced by all bus devices?

    addr &= ~3; // 4 byte aligned value used to simplify read logic.

    const unsigned width = stdc_count_ones(mask);
    u32 ret;

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        ret = Bus_MainRAM_Read(sys, &sys->AHB9, timestamp_max/*ah shit*/, addr, mask, atomic, hold, seq);
        break;

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        WriteContention(&sys->AHB9, Dev_SWRAM, *seq);
        Timing32(&sys->AHB9);
        switch(sys->IO.WRAMCR)
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
        WriteContention(&sys->AHB9, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
        Timing32(&sys->AHB9); // checkme: does all of IO have the exact same timings?
        ret = IO9_Read(sys, addr, mask);
        break;

    case 0x05: // 2D GPU Palette
        /*LogPrint(LOG_UNIMP|LOG_ARM9,*/ CrashSpectacularly("NTR_AHB9: Unimplemented READ%i: PALETTE\n", width);
        // TODO: 2d gpu contention timings
        WriteContention(&sys->AHB9, Dev_Palette, *seq);
        Timing16(&sys->AHB9, mask);
        ret = MemoryRead(32, sys->Palette, addr, Palette_Size);
        break;

    case 0x06: // VRAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented READ%i: VRAM\n", 16);
        // im going to cry
        Timing16(&sys->AHB9, mask); // placeholder
        ret = 0; // TODO
        break;

    case 0x07: // 2D GPU OAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented READ%i: OAM\n", width);
        // TODO: 2d gpu contention timings
        WriteContention(&sys->AHB9, Dev_Palette, *seq);
        Timing32(&sys->AHB9);
        ret = MemoryRead(32, sys->OAM, addr, OAM_Size);
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
            Timing32(&sys->AHB9);
            ret = MemoryRead(32, sys->NTRBios9, addr, NTRBios9_Size);
            break;
        }
        else [[fallthrough]];

    default: // Unmapped Device;
        LogPrint(LOG_ALWAYS, "%08lX %08lX %08lX\n", sys->ARM9.CP15.CR.Raw, sys->ARM9.CP15.ITCMCR.Raw, sys->ARM9.CP15.DTCMCR.Raw);
        /*LogPrint(LOG_ODD|LOG_ARM9,*/ CrashSpectacularly("NTR_AHB9: %i bit read from unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        Timing32(&sys->AHB9);
        ret = 0; // always reads 0
        break;
    }

    *ts = sys->AHB9.Timestamp;
    return ret;
}

void AHB9_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, const bool hold, bool* seq)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, does that also apply to arm9?
    // if so, is the alignment properly enforced by all bus devices?

    addr &= ~3; // 4 byte aligned value used to simplify read logic.

    const unsigned width = stdc_count_ones(mask);

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        if (addr < 0x02000100) LogPrint(LOG_ALWAYS, "%08lX\n", val);
        Bus_MainRAM_Write(sys, &sys->AHB9, timestamp_max/*ah shit*/, addr, val, mask, atomic, hold, seq);
        break;

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        WriteContention(&sys->AHB9, Dev_SWRAM, *seq);
        Timing32(&sys->AHB9);
        AddWriteContention(&sys->AHB9, Dev_SWRAM);
        switch(sys->IO.WRAMCR)
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
        WriteContention(&sys->AHB9, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
        Timing32(&sys->AHB9); // checkme: does all of IO have the exact same timings?
        AddWriteContention(&sys->AHB9, Dev_IO);
        IO9_Write(sys, addr, val, mask);
        break;

    case 0x05: // 2D GPU Palette
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented WRITE%i: PALETTE\n", width);
        // TODO: 2d gpu contention timings
        WriteContention(&sys->AHB9, Dev_Palette, *seq);
        Timing16(&sys->AHB9, mask);
        AddWriteContention(&sys->AHB9, Dev_Palette);
        MemoryWrite(32, sys->Palette, addr, Palette_Size, val, mask);
        break;

    case 0x06: // VRAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented WRITE%i: VRAM\n", 16);
        // im going to cry
        Timing16(&sys->AHB9, mask); // placeholder
        // TODO
        break;

    case 0x07: // 2D GPU OAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_AHB9: Unimplemented WRITE%i: OAM\n", width);
        // TODO: 2d gpu contention timings
        WriteContention(&sys->AHB9, Dev_OAM, *seq);
        Timing32(&sys->AHB9);
        AddWriteContention(&sys->AHB9, Dev_OAM);
        MemoryWrite(32, sys->OAM, addr, OAM_Size, val, mask);
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
        /*LogPrint(LOG_ODD|LOG_ARM9,*/ CrashSpectacularly("NTR_AHB9: %i bit write to unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        Timing32(&sys->AHB9);
        // always reads 0
        break;
    }

    *ts = sys->AHB9.Timestamp;
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
    if (*cur < bus->Timestamp) *cur = bus->Timestamp;

    // check if anything else is able to run
    if (!atomic && (*cur >= DMA_TimeNextScheduled(sys->DMA9.ChannelTimestamps, priority)))
    {
        // TODO: run higher priority components here

        // catch up component to bus again; otherwise catch bus up to component
        if (*cur < bus->Timestamp) *cur = bus->Timestamp;
        else bus->Timestamp = *cur;

        return false;
    }
    if (bus->Timestamp < *cur) bus->Timestamp = *cur;

    return true;
}

u32 AHB7_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, this presumably still applies to arm9.
    // is the alignment properly enforced by all bus devices?

    addr &= ~3; // 4 byte aligned value used to simplify read logic.

    const unsigned width = stdc_count_ones(mask);
    u32 ret;

    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x000: // NDS BIOS
        // CHECKME: bios contention?
        // TODO: Bios protection.
        Timing32(&sys->AHB7);
        ret = MemoryRead(32, sys->NTRBios7, addr, NTRBios7_Size);
        break;

    case 0x020: // Main RAM
    case 0x028:
        ret = Bus_MainRAM_Read(sys, &sys->AHB7, timestamp_max/*ah shit*/, addr, mask, atomic, hold, seq);
        break;

    case 0x030: // Shared WRAM
        // CHECKME: does it still have distinct contention even if unmapped?
        WriteContention(&sys->AHB7, Dev_SWRAM, *seq);
        Timing32(&sys->AHB7);
        switch(sys->IO.WRAMCR)
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
        WriteContention(&sys->AHB7, Dev_A7WRAM, *seq);
        Timing32(&sys->AHB7);
        ret = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size);
        break;

    case 0x040: // Memory Mapped IO
        WriteContention(&sys->AHB7, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
        Timing32(&sys->AHB7); // checkme: does all of IO have the exact same timings?
        ret = IO7_Read(sys, addr, mask);
        break;

    case 0x060: // VRAM
    case 0x068: // VRAM
        LogPrint(LOG_UNIMP|LOG_ARM7, "NTR_AHB7: Unimplemented READ%i: VRAM\n", 16);
        // im going to cry
        Timing16(&sys->AHB7, mask); // placeholder
        ret = 0; // TODO
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
        /*LogPrint(LOG_ODD|LOG_ARM7,*/ CrashSpectacularly("NTR_AHB7: %i bit read from unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        Timing32(&sys->AHB7);
        ret = 0; // always reads 0
        break;
    }

    *ts = sys->AHB7.Timestamp;
    return ret;
}

void AHB7_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, const bool hold, bool* seq)
{
    // CHECKME: alignment is enforced by the bus on arm7 on gba, this presumably still applies to arm9.
    // is the alignment properly enforced by all bus devices?

    addr &= ~3; // 4 byte aligned value used to simplify write logic.

    const unsigned width = stdc_count_ones(mask);

    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x020: // Main RAM
    case 0x028: // Main RAM
        if (addr < 0x02000100) LogPrint(LOG_ALWAYS, "%08lX\n", val);
        Bus_MainRAM_Write(sys, &sys->AHB7, timestamp_max/*ah shit*/, addr, val, mask, atomic, hold, seq);
        break;

    case 0x030: // Shared WRAM
        // CHECKME: does it still have distinct contention even if unmapped?
        WriteContention(&sys->AHB7, Dev_SWRAM, *seq);
        Timing32(&sys->AHB7);
        AddWriteContention(&sys->AHB7, Dev_SWRAM);
        switch(sys->IO.WRAMCR)
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
        // NOTE: it seems to still have write contention even if unmapped?
        WriteContention(&sys->AHB7, Dev_A7WRAM, *seq);
        Timing32(&sys->AHB7);
        AddWriteContention(&sys->AHB7, Dev_A7WRAM);
        MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, val, mask);
        break;

    case 0x040: // Memory Mapped IO
        WriteContention(&sys->AHB7, Dev_IO, *seq); // checkme: does all of IO have write contention at the same time?
        Timing32(&sys->AHB7); // checkme: does all of IO have the exact same timings?
        AddWriteContention(&sys->AHB7, Dev_IO);
        IO7_Write(sys, addr, val, mask);
        break;
    case 0x048: // 

    case 0x060: // VRAM
    case 0x068: // VRAM
        LogPrint(LOG_UNIMP|LOG_ARM7, "NTR_AHB7: Unimplemented WRITE%i: VRAM\n", 16);
        // im going to cry
        Timing16(&sys->AHB7, mask); // placeholder
        // TODO
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
        /*LogPrint(LOG_ODD|LOG_ARM7,*/ CrashSpectacularly("NTR_AHB7: %i bit write to unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        Timing32(&sys->AHB7);
        break;
    }

    *ts = sys->AHB7.Timestamp;
}
