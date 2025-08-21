#if 0
#include <stdio.h>
#include <stdbit.h>
#include "bus.h"
#include "../utils.h"
#include "../arm/arm9/arm.h"



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
struct BusRet Bus_MainRAM_Read(struct Bus* BusCur, struct BusMainRAM* BusMR, const timestamp MainRAMThreatTS, u32 addr, const u8 accesswidth, const bool atomic, const bool seq, const bool hold)
{
    struct BusRet ret;

    // if main ram is still busy the accessing bus needs to wait until it's available to begin a new burst to it
    if (!seq && (BusCur->Timestamp < BusMR->Timestamp))
        BusCur->Timestamp = BusMR->Timestamp;

    // if there is even the slightest chance that the other bus can access main ram before us we need to catch it up.
    if (BusCur->Timestamp < MainRAMThreatTS)
    {
        ret.Device = Dev_Wait;
    }

    // ok we actually have bus permission now!!! yipee!!!
}

struct BusRet Bus9_Read(struct Bus* Bus9, const u32 addr, const u8 accesswidth, const bool atomic, const bool seq)
{
    // CHECKME: alignment is enforced by the bus on arm7, does that also apply to arm9?
    // CHECKME: is the alignment properly enforced by all bus devices?

    u32 alignaddr = addr & ~0x3; // 4 byte alignment used for most cases to simplify logic.
    struct BusRet ret;

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: MAINRAM\n", (accesswidth+1)*8);
        //return Bus_MainRAM_Read(addr);
        // temp

    case 0x03: // Shared WRAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: SWRAM\n", (accesswidth+1)*8);
        // something something logic goes here

    case 0x04: // Memory Mapped IO
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: IO\n", (accesswidth+1)*8);
        // now this'll need a lot of logic

    case 0x05: // 2D GPU Palette
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: PALETTE\n", (accesswidth+1)*8);
        // TODO

    case 0x06: // VRAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: VRAM\n", (accesswidth+1)*8);
        // im going to cry

    case 0x07: // 2D GPU OAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: OAM\n", (accesswidth+1)*8);
        // TODO

    case 0x08 ... 0x09: // GBA Cartridge ROM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: GBAROM\n", (accesswidth+1)*8);
        // TODO
        // temp

    case 0x0A: // GBA Cartridge RAM
        LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: GBARAM\n", (accesswidth+1)*8);
        // TODO
        // temp

    case 0xFF: // NDS BIOS
        if ((addr & 0xFFFFF000) == 0xFFFF0000) // CHECKME: are these bounds 100% correct?
        {
            LogPrint(LOG_UNIMP|LOG_ARM9, "NTR_BUS9: Unimplemented READ%i: BIOS\n", (accesswidth+1)*8);
            // read bios
        }
        [[fallthrough]]; // this may or may not count as a distinct device? or as part of bios still.

    default: // Dummy Device;
        LogPrint(LOG_ODD|LOG_ARM9, "NTR_BUS9: %i bit read from unmapped memory at 0x%08X? Something went wrong?\n", (accesswidth+1)*8, addr);
        ret.Val = 0; // always returns 0.
        ret.Cycles = 1;
        ret.Device = Dev_Dummy; // does this cause write contention?
        break;
    }

    // check if device is still busy
    if (ret.Device == Bus9->BusyDevice)
    {
        if (Bus9->Timestamp < Bus9->BusyDeviceTS)
            ret.Cycles += 1;
    }

    return ret;
}

/*
    (s64) current streamed address: used for which word is next to be fetched (could check if ((addr & 0x1F) == 1C) at the end of each word fetched to determine if we should end?)
    (s64) expected fetch address: 
*/

void ARM9_CacheStreamingLoop()
{
    if (Bus9Busy) return;
    if (!CacheStreamingActive) return;

    QueueBus9();
}

void ARM9_CacheStreamingCallback()
{
    *StreamRet = fetch;
    ARm9NextStreamFetch = ARm9StreamingAddr;
    ARM9StreamEndTime = ts???;

    if (CacheStreamingAddr == StreamEndAddr)
    {
        CacheStreamingAddr = -1;
    }
    else
    {
        CacheStreamingAddr+=4;
    }
}

// internal bus logic needs to be *capable* of being postponed; but shouldn't always need to be.
// a ns bus access will be stalled if it's busy filling a cacheline (haven't tested aborts)

bool ARM9_InstructionRead(struct ARM946ES* ARM9, const u32 addr)
{
    // note: it should be possible to combine every single major lookup into three 1MiB lookup tables and one big switch statement?
    // (worth it for data accesses?)
    // 0 == abort
    // 1 == itcm
    // 2 == dtcm
    // 3 == cache
    // 4 == ext.bus

    // todo: only do for thumb
    if (ARM9->LatchedAddr == addr)
    {
        // 1 cycle
        // contentions w/ data itcm accesses
        ARM9->ARM.Instr[2] = ARM9->LatchedWord;
        return true;
    }

    if (ARM9->NextIStream >= 0) // check if cache streaming is active
    {
        if (ARM9->NextIStream == (addr & ~0x3))
        {
            // we are on time
            // now we just wait until the word is fetched
            ARM9->IStreamWait = true;
            return false;
        }
        else
        {

        }
    }


    // mpu x perm check

    // itcm enable check
    // itcm readable check(?)
    // itcm check

    // dtcm cannot be accessed on the instruction bus

    // icacheable check

    // bus align?

    // weird stall? (seems to be atomic with dma...? but not with the arm7 for main ram...?)

    // i.bus never drains write buffer

    // access external bus
}

void ARM9_DataRead32()
{
    // mpu r perm check

    // itcm enable check
    // itcm readable check
    // itcm check

    // dtcm enable check
    // dtcm readable check
    // dtcm check

    // dcacheable check

    // bus align?

    // weird stall? (seems to be atomic with dma...? but not with the arm7 for main ram...?)

    // dcache/buffer perm check (write buffer drain)

    // access external bus
}

void ARM9_DataWrite32()
{
    // mpu w perm check

    // itcm enable check
    // itcm check

    // dtcm enable check
    // dtcm check

    // dcache filled check (does not fill line)

    // dcache/buffer perm check (use write buffer)

    // bus align?

    // weird stall? (seems to be atomic with dma...? but not with the arm7 for main ram...?)

    // write buffer drain

    // access external bus
}

// arm9 accesses on the external bus seem to occur as follows:
// 1. arm9 waits for the next rising edge of the bus clock.
// 2. arm9 negotiates control of the bus.
// 3. arm9 puts data into some ungodly slow buffer. (speculation).
// 4. memory access actually occurs.
//
// some more speculation:
// when a burst crosses a 1KiB boundary that's probably the ARM9 restarting the burst as it does not re-incur the initial latency.
// when a burst crosses a device boundary that's probably either the device or bus restarting the burst, as it re-incurs the initial latency.
void ARM9_BusCallback(struct Bus* Bus9, struct ARM946ES* ARM9)
{
    int clockmult = ARM9->BoostedClock ? 2 : 1; // shift

    while(true)
    {
        if (StartBurst)
        {
            // Presumably implements a buffering method described in the ARM946E-S reference manual?
            // It would make the most sense with how this latency behaves.
            int latency = ARM9->BoostedClock ? 8 : 6;
            ARM9->InstrBusTS += latency;
        }

        // if the internal bus is not already >= the current bus time then the buffering latency is hidden.
        if ((ARM9->InstrBusTS >> clockmult) < Bus9->Timestamp)
            ARM9->InstrBusTS = Bus9->Timestamp << clockmult;

        // things that must be passed along:
        // nonsequential vs sequential
        // atomicity
        // address
        // burst width
        // maybe burst type...?
        // burst direction
        // some logic might be needed for DMA, since it somehow handles burts on two devices at once?

        struct BusRet ret = Bus9_Access(); // idk

        if (ret.Device == Dev_Wait) // todo: do something?

        if (!StartBurst && (Bus9->BusyDevice != ret.Device))
        {
            StartBurst = true;
            continue;
        }
        else break;
    }

    // somehow we need a way to handle any post processing the cpu might do.
    // probably use a callback here?

    // set memory to value fetched here?
    ARM9->InstrBusTS = ((Bus9->Timestamp) << clockmult);

    // TODO: write buffer needs special handling for this.
    // Bus is polled by the ARM9 on it's rising clock edge, so we do this.
}

void ODMA9_BusCallback()
{

}

void Bus9_ResolvePriorities(struct Bus* Bus9)
{
    if (!Bus9->CurRequests) return;

    int cur = stdc_trailing_zeros(Bus9->CurRequests);
    switch (cur)
    {
        case PRIO_ODMA:
            break;
        case PRIO_ARM9:
            break;
    }
}

inline int Timing16Bit(const u8 accesswidth)
{
    if (accesswidth == BusAccess_32Bit)
    {
        return 2;
    }
    else return 1;
}
#endif