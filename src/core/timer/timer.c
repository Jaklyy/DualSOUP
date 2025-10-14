#include "timer.h"




void Timer_AddTicks(struct Timer timers[4], const int timernum, timestamp ticks)
{
    struct Timer* timer = &timers[timernum];

    u32 remaining = (0x10000 - timer->Counter);

    u32 numoverflows = 0;

    if (ticks >= remaining) // overflow occured; reload needed.
    {
        // logic handles multiple overflows just in case that comes up.
        u32 iterlen = 0x10000 - timer->Reload;
        timer->Counter = (ticks % iterlen) + timer->Reload;
        numoverflows = (ticks / iterlen) + 1;

        if ((timernum != 3) // not the last timer
            && timers[timernum+1].Control.OverflowTick // next timer ticks when we overflow
            && timers[timernum+1].Control.Enable) // next timer is running
            Timer_AddTicks(timers, timernum+1, numoverflows); // tick the next timer
    }
    else
    {
        timer->Counter += ticks;
    }
}

void Timer_Run(struct Timer timers[4], const timestamp until)
{
    // probably not strictly required to always run all 4 timers.
    // but doing so keeps the logic simple.
    for (int i = 0; i < 4; i++)
    {
        struct Timer* timer = &timers[i];

        if (timer->Control.OverflowTick) continue; // these timers dont tick normally
        if (!timer->Control.Enable) continue; // timer not running

        // this divider behavior probably needs more verification?
        timestamp ticks = (until >> timer->DividerShift) - (timer->LastUpdated >> timer->DividerShift);

        Timer_AddTicks(timers, i, ticks);
    }
}

void Timer_IOWriteHandler(struct Timer timers[4], const u32 addr, u32 val, const u32 mask)
{
    int timerno = ((addr & 0xF) / 4) % 4;

    struct Timer* timer = &timers[timerno];

    val &= ((timerno == 0) ? 0xC3'FFFF : 0xC7'FFFF);
    MaskedWrite(timer->BufferedRegs, val, mask);
}
