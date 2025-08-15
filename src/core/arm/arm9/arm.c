#include "arm.h"
#include "../arm_shared/arm.h"
#include "../../utils.h"




#define cpu ((struct ARM*)ARM9)

void ARM9_Init(struct ARM946ES* ARM9)
{
    cpu->CPUID = ARM9ID;
    // msb of mode is always set
    cpu->CPSR.ModeMSB = 1;

    ARM9_Reset(ARM9); // raise reset exception
}

void ARM9_Reset(struct ARM946ES* ARM9)
{
    // todo: how many cycles does this take?

    // note: 

    // note: arm docs explicitly state that R14_SVC and SPSR_SVC have an "unpredictable value" when reset is de-asserted
    // which could mean literally anything
    // it is entirely possible that the old pc and cpsr are banked by the processor
    // or at least it tries to and instead puts some nonsense in them?
    // ...or it could just mean that they aren't reset in any way......

    // todo: does cache prng ever get reset? (does it ever get explicitly initialized?)

    // reset cpsr bits
    // flag bits dont seem to be mentioned anywhere?
    cpu->CPSR.Mode = MODE_SWI;
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    // reset control reg
    ARM9->CP15.CR.ITCMLoadMode = false;
    ARM9->CP15.CR.DTCMLoadMode = false;
    ARM9->CP15.CR.DTCMEnable = false;
    ARM9->CP15.CR.NoLoadTBit = false;
    ARM9->CP15.CR.CacheRR = false;
    ARM9->CP15.CR.ICacheEnable = false;
    ARM9->CP15.CR.BigEndian = false;
    ARM9->CP15.CR.DCacheEnable = false;
    ARM9->CP15.CR.MPUEnable = false;

    // these two are configurable via input pins.
    // exception vector is obviously default set since that's where our bootcode is.
    // itcm enable is less clear, it's probably not important since all relevant bootroms
    // should explicitly set this before using it, but it'd be nice to know ig.
    ARM9->CP15.CR.ITCMEnable = false;
    ARM9->CP15.CR.HiVector = true;

    // could technically hardcode this exception vector check, but eh.
    cpu->PC = ARM9_GetExceptionBase(ARM9) + VECTOR_RST;
}

u32 ARM9_GetExceptionBase(struct ARM946ES* ARM9)
{
    return (ARM9->CP15.CR.HiVector ? 0xFFFF0000 : 0x00000000);
}

u32 ARM9_GetReg(struct ARM946ES* ARM9, const int reg)
{
    // todo: strd/ldrd incorrect forwarding errata

    return cpu->R[reg];
}

void ARM9_SetPC(struct ARM946ES* ARM9, const int reg, u32 val, const int iloffs)
{
    // arm9 enforces pc alignment properly for once.
    val &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3);
    // r15 interlocks must be resolved immediately.
    cpu->Timestamp = ARM9->MemTimestamp + iloffs;

    // pipeline flush logic goes here.
}

void ARM9_SetReg(struct ARM946ES* ARM9, const int reg, u32 val, const int iloffs, const int iloffs_c)
{
    if (reg == 15) // PC must be handled specially
    {
        ARM9_SetPC(ARM9, reg, val, iloffs); // CHECKME: should this be the port C time?
    }
    else
    {
        // I pray that nothing makes it any more complex than this.
        cpu->R[reg] = val;
        ARM9->RegIL[reg][0] = iloffs;
        ARM9->RegIL[reg][1] = iloffs_c;
    }
}

void ARM9_UpdateInterlocks(struct ARM946ES* ARM9, const s8 diff)
{
    // i spent some time writing simd for this manually but it's so simple auto-simd was just as good.
    #pragma GCC ivdep
    for (int i = 0; i < 32; i++)
    {
        ARM9->RegIL[i & 0xF][i>>4] -= diff;
        if (ARM9->RegIL[i & 0xF][i>>4] < 0) ARM9->RegIL[i & 0xF][i>>4] = 0;
    }
}

void ARM9_CheckInterlocks(struct ARM946ES* ARM9, int* offset, const int reg, const int cycledelay, const bool portc)
{
    // the fact this always needs a branch really annoys me.
    // but i dont think this is possible to work around without losing accuracy.
    s8 diff = ARM9->RegIL[reg][portc] - cycledelay;
    if (diff > 0)
    {
        cpu->Timestamp = ARM9->MemTimestamp + diff - 1;
    }
}

void ARM9_ExecuteCycles(struct ARM946ES* ARM9, const u32 Execute, const u32 Memory)
{
    cpu->Timestamp += Execute;

    s8 diff = ARM9->MemTimestamp - (cpu->Timestamp + Memory);
    ARM9->MemTimestamp += diff;

    ARM9_UpdateInterlocks(ARM9, diff);
}

// original plan wont work
// we need:
// decode interlocks
// fetch
// execute
// memory
// in that order, otherwise we can't get interrupts cycle accurate.

inline void ARM9_Step(struct ARM946ES* ARM9)
{
    //Decode
    //if (!Fetch) return;

    //Execute


}

inline void ARM9_ResolveTimings(struct ARM946ES* ARM9)
{
    //if (interlocked)

    // (memory - 1) + (fetch - 1) + execute
}

#undef cpu
