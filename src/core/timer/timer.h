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
            } Control;
        };
    };
    u16 Counter;
    u8 DividerShift;
    bool NeedsUpdate;
    bool NeedsInit;
    u32 BufferedRegs;
};

void Timer_IOWriteHandler(struct Timer* timers, const timestamp curts, const u32 addr, u32 val, const u32 mask);
u32 Timer_IOReadHandler(struct Timer* timers, const timestamp curts, const u32 addr);
