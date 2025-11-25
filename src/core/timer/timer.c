#include "timer.h"




bool Timer_AddTicks(struct Timer* timers, const int timernum, timestamp ticks)
{
    struct Timer* timer = &timers[timernum];

    u32 remaining = (0x10000 - timer->Counter);

    u32 numoverflows = 0;

    if (ticks >= remaining) // overflow occured; reload needed.
    {
        ticks -= remaining;
        // logic handles multiple overflows just in case that comes up.
        u32 iterlen = 0x10000 - timer->Reload;
        timer->Counter = (ticks % iterlen) + timer->Reload;
        numoverflows = (ticks / iterlen) + 1;


        if ((timernum != 3) // not the last timer
            && timers[timernum+1].Control.OverflowTick // next timer ticks when we overflow
            && timers[timernum+1].Control.Enable) // next timer is running
        {
            Timer_AddTicks(timers, timernum+1, numoverflows); // tick the next timer
        }
        return (timer->Counter == timer->Reload); // overflowed on last cycle
    }
    else
    {
        timer->Counter += ticks;
        return false;
    }
}

void Timer_UpdateCR(struct Timer* timer)
{
    bool oldenable = timer->Control.Enable;

    timer->Regs = timer->BufferedRegs;

    if (!oldenable && timer->Control.Enable)
    {
        timer->NeedsInit = true;
    }

    timer->DividerShift = ((timer->Control.Divider == 0) ? 0 : ((timer->Control.Divider * 2) + 4));
}

u8 Timer_Run(struct Timer* timers, const timestamp until)
{
    u8 overflowed = 0;
    // make sure they're all updated.
    for (int i = 0; i < 4; i++)
    {
        struct Timer* timer = &timers[i];

        if (timer->LastUpdated >= until) continue;

        if (!timer->Control.OverflowTick && timer->Control.Enable) // timer not running
        {
            // this divider behavior probably needs more verification?
            timestamp ticks = ((timer->LastUpdated+1) >> timer->DividerShift) - (timer->LastUpdated >> timer->DividerShift);

            overflowed |= Timer_AddTicks(timers, i, ticks) << i;
        }
        timer->LastUpdated += 1;

        if (timer->NeedsUpdate)
        {
            Timer_UpdateCR(timer);
            timer->NeedsUpdate = false;
        }

        // CHECKME: what happens if it gets reloaded at the same time it ticks via overflow?
        if (timer->NeedsInit || overflowed)
        {
            if ((timer->LastUpdated >= until) && !overflowed) continue;
            timer->Counter = timer->Reload;
            timer->NeedsInit = false;
        }
    }

    // probably not strictly required to always run all 4 timers.
    // but doing so keeps the logic simple.
    for (int i = 0; i < 4; i++)
    {
        struct Timer* timer = &timers[i];

        if (timer->LastUpdated >= until) continue;
        if (timer->Control.OverflowTick) { timer->LastUpdated = until; continue; }// these timers dont tick normally
        if (!(timer->Control.Enable)) { timer->LastUpdated = until; continue; } // timer not running

        // this divider behavior probably needs more verification?
        timestamp ticks = (until >> timer->DividerShift) - (timer->LastUpdated >> timer->DividerShift);

        overflowed |= Timer_AddTicks(timers, i, ticks) << i;
        timer->LastUpdated = until; 
    }
    return overflowed;
}

void Timer_IOWriteHandler(struct Timer* timers, const timestamp curts, const u32 addr, u32 val, const u32 mask)
{
    u8 overflowmask = Timer_Run(timers, curts+1);

    int timerno = ((addr & 0xF) / 4) % 4;

    struct Timer* timer = &timers[timerno];

    val &= ((timerno == 0) ? 0xC3'FFFF : 0xC7'FFFF);
    MaskedWrite(timer->BufferedRegs, val, mask);

    if (!(val & mask & (1<<7)))
    {
        // bug occured; overflow it an extra time
        // CHECKME: does this happen immediately?
        Timer_UpdateCR(timer);
        if ((overflowmask & (1<<timerno))
            && (timerno != 3) // not the last timer
            && timers[timerno+1].Control.OverflowTick // next timer ticks when we overflow
            && timers[timerno+1].Control.Enable) // next timer is running
        {
            Timer_AddTicks(timers, timerno+1, 1); // tick the next timer
        }
    }
    else timer->NeedsUpdate = true;
}

u32 Timer_IOReadHandler(struct Timer* timers, const timestamp curts, const u32 addr)
{
    Timer_Run(timers, curts);

    int timerno = ((addr & 0xF) / 4) % 4;

    return timers[timerno].Control.Raw << 16 | timers[timerno].Counter;
}
