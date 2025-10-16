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

bool ARM9_ITCMTryRead(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !ARM9->CP15.CR.ITCMLoadMode && !(addr >> ARM9->CP15.ITCMShift));
}

bool ARM9_ITCMTryWrite(const struct ARM946ES* ARM9, const u32 addr)
{
    return (ARM9->CP15.CR.ITCMEnable && !(addr >> ARM9->CP15.ITCMShift));
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
u32 ARM9_InstrRead(struct ARM946ES* ARM9, const u32 addr)
{
    // todo: ns needs to check if cache streaming active
    // should probably be done in the pipeline flush logic?
    // CHECKME: though what happens if you switch fetch size without a flush?

    // itcm
    if (ARM9_ITCMTryRead(ARM9, addr))
    {
        // TODO: add contention cycle
        //return ARM9->ITCM32[(addr/4) & (ARM9_ITCMSize-1)];
        ARM9_FetchCycles(ARM9, 1);
        return MemoryRead(32, ARM9->ITCM, addr, ARM9_ITCMSize-1);
    }

    // icache

    // external bus
    // align to bus
    //ARM9_BusClockAlign(ARM9, &ARM9->ARM.Timestamp);
    // TODO: handle queuing of bus logic

    ARM9_FetchCycles(ARM9, 1);
    return 0xDEADBEEF;
}

// arm
void ARM9_InstrRead32(struct ARM946ES* ARM9, const u32 addr)
{
    // todo: ns needs to check if cache streaming active
    // should probably be done in the pipeline flush logic?
    // CHECKME: though what happens if you switch fetch size without a flush?

    // TODO: prefetch abort
    if (false)
    {
        ARM9_FetchCycles(ARM9, 1);
        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = 0xE1200070, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                                .Aborted = true,
                                                .CoprocPriv = false}; // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
    }
    else
    {
        ARM9_InstrRead(ARM9, addr);
        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = ARM9_InstrRead(ARM9, addr),
                                                .Aborted = false,
                                                .CoprocPriv = ARM9->ARM.Privileged};
    }
}

// thumb
void ARM9_InstrRead16(struct ARM946ES* ARM9, const u32 addr)
{
    // todo: ns needs to check for cache streaming active
    // should probably be done in the pipeline flush logic?
    // CHECKME: though what happens if you switch fetch size without a flush?

    // TODO: prefetch abort
    if (false)
    {
        ARM9_FetchCycles(ARM9, 1);
        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = 0xBE00, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                                .Aborted = true,
                                                .CoprocPriv = false}; // privilege bug shouldn't matter here; aborted instrs aren't coprocessor instructions.
    }
    else
    {
        u32 instr;
        if ((addr & 2) && ARM9->ARM.CodeSeq) // note: not 100% sure if this logic is correct but it probably doesn't matter...?
        {
            // use latched halfword
            instr = ARM9->LatchedHalfword;
        }
        else
        {
            instr = ARM9_InstrRead(ARM9, addr);

            if (addr & 2)
            {
                // correct instruction alignment.
                // TODO: I think this is wrong for big endian
                instr >>= 16;
            }
            else
            {
                // save other halfword for later
                // TODO: I think this is wrong for big endian
                // CHECKME: does latching apply to all access types?
                ARM9->LatchedHalfword = (instr >> 16);

                // NOTE: not sure if clearing the high bits is correct?
                // it's not currently clear if it's possible to switch from thumb -> arm without a pipeline flush on the ARM946E-S, but exact behavior might matter for handling that.
                instr &= 0xFFFF;
            }
        }

        ARM9->ARM.Instr[2] = (struct ARM_Instr){.Raw = instr,
                                                .Aborted = false,
                                                .CoprocPriv = false}; // privilege bug shouldn't matter here; thumb does not have coprocessor instructions.
    }
}

void ARM9_LoadRegister32_Callback()
{

}

void ARM9_LoadRegister32Unaligned_Callback()
{

}
