#include <stdckdint.h>
#include "../console.h"
#include "timer.h"




extern u8 Timer_Run(struct Console* sys, struct Timer* timers, const timestamp until, bool a9);

void Timer_SchedRun9(struct Console* sys, timestamp now)
{
    Timer_Run(sys, sys->Timers9, now, true);
}

void Timer_SchedRun7(struct Console* sys, timestamp now)
{
    Timer_Run(sys, sys->Timers7, now, false);
}

void Timer_CalcNextIRQ(struct Console* sys, bool a9)
{
    struct Timer* timers = ((a9) ? sys->Timers9 : sys->Timers7);
    timestamp timerrem[4];
    timestamp timerper[4];
    timestamp nextirq[4] = {timestamp_max, timestamp_max, timestamp_max, timestamp_max};
    for (int i = 0; i < 4; i++)
    {
        if (timers[i].Control.Enable)
        {
            timerrem[i] = timestamp_max;
            timerper[i] = timestamp_max;
        }
        timerrem[i] = (0x10000 - timers[i].Counter);
        timerper[i] = (0x10000 - timers[i].Reload);

        if (timers[i].Control.OverflowTick)
        {
            if (ckd_mul(&timerrem[i], timerrem[i]-1, timerper[i-1]))
            {
                timerrem[i] = timestamp_max;
            }
            else if (ckd_add(&timerrem[i], timerrem[i], timerrem[i-1]))
            {
                timerrem[i] = timestamp_max;
            }

            if (ckd_mul(&timerper[i], timerper[i], timerper[i-1]))
            {
                timerrem[i] = timestamp_max;
            }
        }
        if (timers[i].Control.IRQ)
            nextirq[i] = timerrem[i];
    }

    timestamp next = timestamp_max;
    for (int i = 0; i < 4; i++)
    {
        if (next > nextirq[i])
            next = nextirq[i];
    }

    if (a9)
        Schedule_Event(sys, Timer_SchedRun9, Evt_Timer9, next);
    else
        Schedule_Event(sys, Timer_SchedRun7, Evt_Timer7, next);
}

bool Timer_AddTicks(struct Console* sys, struct Timer* timers, const int timernum, timestamp ticks, bool a9)
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
            Timer_AddTicks(sys, timers, timernum+1, numoverflows, a9); // tick the next timer
        }

        if (timers[timernum].Control.IRQ)
            Console_ScheduleIRQs(sys, IRQ_Timer0+timernum, a9, timers[timernum].LastUpdated); // last update is kinda wrong to use but probably good enough tbh :: checkme: delay?

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

u8 Timer_Run(struct Console* sys, struct Timer* timers, const timestamp until, bool a9)
{
    u8 overflowed = 0;
    // make sure they're all updated.
    for (int i = 0; i < 4; i++)
    {
        struct Timer* timer = &timers[i];

        if (timer->LastUpdated >= until) continue;

        timer->LastUpdated += 1;
        if (!timer->Control.OverflowTick && timer->Control.Enable) // timer not running
        {
            // this divider behavior probably needs more verification?
            timestamp ticks = ((timer->LastUpdated) >> timer->DividerShift) - ((timer->LastUpdated-1) >> timer->DividerShift);

            overflowed |= Timer_AddTicks(sys, timers, i, ticks, a9) << i;
        }

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

        timer->LastUpdated = until; 
        overflowed |= Timer_AddTicks(sys, timers, i, ticks, a9) << i;
    }
    Timer_CalcNextIRQ(sys, a9);
    return overflowed;
}

void Timer_IOWriteHandler(struct Console* sys, struct Timer* timers, const timestamp curts, const u32 addr, u32 val, const u32 mask, const bool a9)
{
    u8 overflowmask = Timer_Run(sys, timers, curts+1, a9);

    int timerno = ((addr & 0xF) / 4) % 4;

    struct Timer* timer = &timers[timerno];

    val &= ((timerno == 0) ? 0xC3'FFFF : 0xC7'FFFF);
    MaskedWrite(timer->BufferedRegs, val, mask);

    if (!(val & mask & (1<<(7+16))))
    {
        // bug occured; overflow it an extra time
        // CHECKME: does this happen immediately?
        Timer_UpdateCR(timer);
        if ((overflowmask & (1<<timerno))
            && (timerno != 3) // not the last timer
            && timers[timerno+1].Control.OverflowTick // next timer ticks when we overflow
            && timers[timerno+1].Control.Enable) // next timer is running
        {
            timers[timerno+1].LastUpdated += 1; // checkme?
            Timer_AddTicks(sys, timers, timerno+1, 1, a9); // tick the next timer
        }
    }
    else timer->NeedsUpdate = true;
    Timer_CalcNextIRQ(sys, a9);
}

u32 Timer_IOReadHandler(struct Console* sys, struct Timer* timers, const timestamp curts, const u32 addr, const bool a9)
{
    Timer_Run(sys, timers, curts, a9);

    int timerno = ((addr & 0xF) / 4) % 4;

    return timers[timerno].Control.Raw << 16 | timers[timerno].Counter;
}
