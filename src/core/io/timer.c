#include <stdckdint.h>
#include "../console.h"
#include "timer.h"




extern void Timer_Run(struct Console* sys, struct Timer* timers, const timestamp until, bool a9);

void Timer_SchedRun9(struct Console* sys, timestamp now)
{
    Timer_Run(sys, sys->Timers9, now, true);
}

void Timer_SchedRun7(struct Console* sys, timestamp now)
{
    Timer_Run(sys, sys->Timers7, now, false);
}

void Timer_CalcNextIRQ(struct Console* sys, timestamp now, bool a9)
{
    struct Timer* timers = ((a9) ? sys->Timers9 : sys->Timers7);
    timestamp timerrem[4];
    timestamp timerper[4];
    timestamp nextirq[4] = {timestamp_max, timestamp_max, timestamp_max, timestamp_max};
    for (int i = 0; i < 4; i++)
    {
        if (!timers[i].On)
        {
            timerrem[i] = timestamp_max;
            timerper[i] = timestamp_max;
            continue;
        }
        timerrem[i] = ((0x10000 - timers[i].Counter) << timers[i].DividerShift) + (now & ((1<<timers[i].DividerShift)-1));
        timerper[i] = ((0x10000 - timers[i].Reload) << timers[i].DividerShift);

        if (timers[i].CR.OverflowTick)
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
        if (timers[i].CR.IRQ)
            nextirq[i] = timerrem[i] + now;
    }

    timestamp next = timestamp_max;
    for (int i = 0; i < 4; i++)
    {
        if (next > nextirq[i])
            next = nextirq[i];
    }

    if (a9) Schedule_Event(sys, Timer_SchedRun9, Evt_Timer9, next);
    else    Schedule_Event(sys, Timer_SchedRun7, Evt_Timer7, next);
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
            && timers[timernum+1].CR.OverflowTick // next timer ticks when we overflow
            && (timers[timernum+1].On)) // next timer is running
        {
            timers[timernum+1].JustOverflowed = Timer_AddTicks(sys, timers, timernum+1, numoverflows, a9); // tick the next timer
        }

        if (timers[timernum].CR.IRQ)
            Console_ScheduleIRQs(sys, IRQ_Timer0+timernum, a9, timers[timernum].LastUpdated); // last update is kinda wrong to use but probably good enough tbh :: checkme: delay?

        return (timer->Counter == timer->Reload); // overflowed on last cycle
    }
    else
    {
        timer->Counter += ticks;
        return false;
    }
}

void Timer_Run(struct Console* sys, struct Timer* timers, const timestamp until, bool a9)
{
    // probably not strictly required to always run all 4 timers.
    // but doing so keeps the logic simple.
    for (int i = 0; i < 4; i++)
    {
        struct Timer* timer = &timers[i];

        if (timer->LastUpdated >= until) continue;
        if (timer->CR.OverflowTick) { timer->LastUpdated = until; continue; }// these timers dont tick normally
        if (!timer->On) { timer->LastUpdated = until; continue; } // timer not running

        // this divider behavior probably needs more verification?
        timestamp ticks = (until >> timer->DividerShift) - (timer->LastUpdated >> timer->DividerShift);

        timer->LastUpdated = until; 
        timer[i].JustOverflowed = Timer_AddTicks(sys, timers, i, ticks, a9);
    }
    Timer_CalcNextIRQ(sys, until, a9);
}

void Timer_InitReload(struct Console* sys, timestamp now, bool a9)
{
    struct Timer* timer = (a9 ? sys->Timers9 : sys->Timers7);
    Timer_Run(sys, timer, now, a9);

    for (int i = 0; i < 4; i++)
    {
        if (timer[i].NeedsEnable)
        {
            timer[i].NeedsEnable = false;
            timer[i].On = true;
            timer[i].Counter = timer[i].Reload;
        }
    }
    Timer_CalcNextIRQ(sys, now, a9);
}

void Timer9_InitReload(struct Console* sys, timestamp now)
{
    Timer_InitReload(sys, now, true);
}

void Timer7_InitReload(struct Console* sys, timestamp now)
{
    Timer_InitReload(sys, now, false);
}

void Timer_UpdateCRs(struct Console* sys, timestamp now, bool a9)
{
    struct Timer* timer = (a9 ? sys->Timers9 : sys->Timers7);
    Timer_Run(sys, timer, now, a9);

    for (int i = 0; i < 4; i++)
    {
        if (timer[i].NeedsUpdate)
        {
            timer[i].NeedsUpdate = false;
            bool oldenable = timer[i].CR.Enable;

            timer[i].Regs = timer[i].BufferedRegs;

            timer->DividerShift = ((timer->CR.Divider == 0) ? 0 : ((timer->CR.Divider * 2) + 4));

            if (!oldenable && timer[i].CR.Enable)
            {
                timer[i].NeedsEnable = true;
            }
            else 
            {
                if (oldenable && !timer[i].CR.Enable)
                {
                    timer[i].On = false;
                    // overflow is detected twice
                    if (timer[i].JustOverflowed && (i != 3) && timer[i+1].CR.OverflowTick && timer[i+1].On)
                    {
                        Timer_AddTicks(sys, timer, i+1, 1, a9);
                        // it overflows twice here idk how im adding that rn
                    }
                }
            }
        }
    }
    if (a9) Schedule_Event(sys, Timer9_InitReload, Evt_Timer9, now+1);
    else    Schedule_Event(sys, Timer7_InitReload, Evt_Timer7, now+1);
}

void Timer9_UpdateCRs(struct Console* sys, timestamp now)
{
    Timer_UpdateCRs(sys, now, true);
}

void Timer7_UpdateCRs(struct Console* sys, timestamp now)
{
    Timer_UpdateCRs(sys, now, false);
}

void Timer_IOWriteHandler(struct Console* sys, struct Timer* timers, const timestamp curts, const u32 addr, const u32 val, const u32 mask, const bool a9)
{
    if (a9) Scheduler_RunEventManual(sys, curts, Evt_Timer9, true);
    else    Scheduler_RunEventManual(sys, curts, Evt_Timer7, false);

    Timer_Run(sys, timers, curts, a9);

    unsigned timerno = ((addr & 0xF) / 4) % 4;
    struct Timer* timer = &(a9 ? sys->Timers9 : sys->Timers7)[timerno];

    u32 mask2 = ((timerno == 0) ? 0xC3'FFFF : 0xC7'FFFF);
    MaskedWrite(timer->BufferedRegs, val, mask & mask2);

    timer->NeedsUpdate = true;

    if (a9) Schedule_Event(sys, Timer9_UpdateCRs, Evt_Timer9, curts+1);
    else    Schedule_Event(sys, Timer7_UpdateCRs, Evt_Timer7, curts+1);
    //Timer_CalcNextIRQ(sys, curts, a9);
}

u32 Timer_IOReadHandler(struct Console* sys, struct Timer* timers, const timestamp curts, const u32 addr, const bool a9)
{
    if (a9) Scheduler_RunEventManual(sys, curts, Evt_Timer9, true);
    else    Scheduler_RunEventManual(sys, curts, Evt_Timer7, false);

    Timer_Run(sys, timers, curts, a9);

    int timerno = ((addr & 0xF) / 4) % 4;

    return timers[timerno].CR.Raw << 16 | timers[timerno].Counter;
}
