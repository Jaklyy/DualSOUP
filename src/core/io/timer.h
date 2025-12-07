#pragma once
#include "../utils.h"




struct Timer
{
    timestamp LastUpdated;
    union
    {
        u32 Regs;
        struct
        {
            u16 Reload;
            union
            {
                u8 Raw;
                struct
                {
                    u8 Divider : 2;
                    bool OverflowTick : 1; // todo: name this something better
                    u8 : 3;
                    bool IRQ : 1;
                    bool Enable : 1;
                };
            } CR;
        };
    };
    u16 Counter;
    u8 DividerShift;
    bool NeedsUpdate;
    bool NeedsEnable;
    bool On;
    bool JustOverflowed;
    u32 BufferedRegs;
};

struct Console;

void Timer_IOWriteHandler(struct Console* sys, struct Timer* timers, const timestamp curts, const u32 addr, u32 val, const u32 mask, const bool a9);
u32 Timer_IOReadHandler(struct Console* sys, struct Timer* timers, const timestamp curts, const u32 addr, const bool a9);