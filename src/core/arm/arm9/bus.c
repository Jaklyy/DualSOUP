#include "arm.h"




void ARM9_BusClockAlign(struct ARM946ES* ARM9, timestamp* ts)
{
    // external bus accesses must be aligned to the external bus' clock.
    unsigned align = ((ARM9->BoostedClock) ? 3 : 1);
    // external bus accesses have a fixed latency.
    // this can begin while other internal arm9 buses are using the external bus, thus hiding part or even all of the latency.
    unsigned latency = ((ARM9->BoostedClock) ? 8 : 6);
    *ts = ((*ts + align) & ~align) + latency;
}

void ARM9_ConfigureITCM(struct ARM946ES* ARM9)
{
    u32 size = ARM9->CP15.ITCMCR.Size + 9;
    if (size < 12) size = 12; // 4KiB min
    // CHECKME does anything interesting happen with a size >32 aka 4GiB?

    ARM9->CP15.ITCMShift = size;
}

bool ARM9_ITCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !ARM9->CP15.CR.ITCMLoadMode && !(addr >> ARM9->CP15.ITCMShift));
}

bool ARM9_ITCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !(addr >> ARM9->CP15.ITCMShift));
}

void ARM9_ConfigureDTCM(struct ARM946ES* ARM9)
{
    bool enabled = ARM9->CP15.CR.DTCMEnable;
    bool writeonly = ARM9->CP15.CR.DTCMLoadMode;

    if (enabled)
    {
        u32 size = ARM9->CP15.DTCMCR.Size + 9;
        if (size < 12) size = 12; // 4KiB min
        // CHECKME does anything interesting happen with a size >32 aka 4GiB?

        u32 base = ARM9->CP15.DTCMCR.Raw >> size;

        ARM9->CP15.DTCMShift = size;
        ARM9->CP15.DTCMWriteBase = base;

        if (writeonly)
        {
            // sort of silly solution
            ARM9->CP15.DTCMReadBase = u64_max;
        }
        else
        {
            ARM9->CP15.DTCMReadBase = base;
        }
    }
    else
    {
        ARM9->CP15.DTCMReadBase = u64_max;
        ARM9->CP15.DTCMWriteBase = u64_max;
    }
}

bool ARM9_DTCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return ((addr >> ARM9->CP15.DTCMShift) == ARM9->CP15.DTCMReadBase);
}

bool ARM9_DTCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return ((addr >> ARM9->CP15.DTCMShift) == ARM9->CP15.DTCMWriteBase);
}

// arm
u32 ARM9_InstrRead32(struct ARM946ES* ARM9, const u32 addr)
{
    // todo: ns needs to check if cache streaming active
    // should probably be done in the pipeline flush logic?
    // CHECKME: though what happens if you switch fetch size without a flush?

    // TODO: prefetch abort
    if (false)
    {
        return 0xE1240671; // bkpt, #0x4061
    }

    // itcm
    if (ARM9_ITCMTryRead(ARM9, addr))
    {
        // TODO: add contention cycle
        //return ARM9->ITCM32[(addr/4) & (ARM9_ITCMSize-1)];
        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize-1);
    }

    // icache

    // external bus
    // align to bus
    ARM9_BusClockAlign(ARM9, &ARM9->ARM.Timestamp);
    // TODO: handle queuing of bus logic

    return u32_max;
}

// thumb
u16 ARM9_InstrRead16(struct ARM946ES* ARM9, const u32 addr)
{
    // todo: ns needs to check for cache streaming active
    // should probably be done in the pipeline flush logic?
    // CHECKME: though what happens if you switch fetch size without a flush?

    // TODO: prefetch abort
    if (false)
    {
        return 0xBE41; // bkpt, #0x41
    }

    // itcm
    if (ARM9_ITCMTryRead(ARM9, addr))
    {
        // TODO: add contention cycle
        ARM9->ARM.Timestamp += 1;
        return ARM9->ITCM16[(addr/2) & (ARM9_ITCMSize-1)];
    }

    // icache

    // latched (is this done here only? how does this work with cache streaming?)

    // external bus
    ARM9_BusClockAlign(ARM9, &ARM9->ARM.Timestamp);
    // TODO: handle queuing of bus logic

    return u16_max;
}
