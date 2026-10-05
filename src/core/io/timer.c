#include <stdckdint.h>
#include "core/console.h"
#include "timer.h"
#include "core/scheduler.h"
#include "core/utils.h"
#include "sound.h"




void Timer_CalcNextEvent(Console* sys, Timer arr[], u8 num, timestamp now, TimerType type)
{
    Timer* timer = &arr[num];

    if (!timer->On || !(timer->CR.IRQ || (type == TimerType_Snd))) return;

    timestamp next = 0;
    goto first;
    do
    {
        timer--;
        next -= 1;
        next *= (0x10000 - timer->Reload) << timer->DividerShift;

        first:
        next += (0x10000 - timer->Counter) << timer->DividerShift;
        next += (now & ((1<<timer->DividerShift)-1));
    } while (timer->CR.OverflowTick);

    next += now;

    Scheduler_Events evt;
    switch(type)
    {
    case TimerType_7:   evt = Evt_Timer70Run; break;
    case TimerType_9:   evt = Evt_Timer90Run; break;
    case TimerType_Snd: evt = Evt_TimerSnd0Run; break;
    }
    Sched_AddEvent(sys, next, evt+num);
}

void Timer_AddTicks(Console* sys, Timer arr[], u8 num, timestamp now, timestamp ticks, TimerType type)
{
    Timer* timer = &arr[num];

    u32 remaining = (0x10000 - timer->Counter);

    u32 numoverflows = 0;

    if (ticks >= remaining) // overflow occured; reload needed.
    {
        ticks -= remaining;
        // logic handles multiple overflows just in case that comes up.
        u32 iterlen = 0x10000 - timer->Reload;
        timer->Counter = (ticks % iterlen) + timer->Reload;
        numoverflows = (ticks / iterlen) + 1;

        if (type == TimerType_Snd) // sound timer; sample audio fifo
            SoundFIFO_Sample(sys, num, now);
        else
        {
            if ((num < 3) // not the last timer
                && arr[num+1].CR.OverflowTick // next timer ticks when we overflow
                && (arr[num+1].On)) // next timer is running
            {
                Timer_AddTicks(sys, arr, num+1, now, numoverflows, type); // tick the next timer
            }

            if (timer->CR.IRQ) // checkme: delay?
                Sched_AddEvent(sys, now+DSClk33(1), ((type == TimerType_9) ? Evt_IRQ9_Time0 : Evt_IRQ7_Time0) + num);
        }
    }
    else timer->Counter += ticks;
}

void Timer_Run(Console* sys, Timer arr[], u8 num, timestamp now, TimerType type)
{
    Timer* timer = &arr[num];

    if (!timer->On) return; // timer not running
    if (timer->CR.OverflowTick) return Timer_Run(sys, arr, num-1, now, type); // run previous timer; NOTE: index 0 cannot have overflow tick enabled, so this is safe (tm)
    if (timer->LastUpdated == now) return; // nothing needs doing.

    // this divider behavior probably needs more verification?
    timestamp ticks = (now >> timer->DividerShift) - (timer->LastUpdated >> timer->DividerShift);
    timer->LastUpdated = now;

    Timer_AddTicks(sys, arr, num, now, ticks, type);
    Timer_CalcNextEvent(sys, arr, num, now, type);
}

void Timer_UpdateCR(Console* sys, Timer arr[], u8 num, timestamp now, TimerType type)
{
    Timer_Run(sys, arr, num, now, type);

    Timer* timer = &arr[num];
    if (timer->NeedsEnable)
    {
        timer->NeedsEnable = false;
        timer->Counter = timer->Reload;
        Timer_CalcNextEvent(sys, arr, num, now, type);
    }
    else if (timer->NeedsUpdate)
    {
        timer->NeedsUpdate = false;
        bool oldenable = timer->CR.Enable;

        timer->Regs = timer->BufferedRegs;

        static_assert(POPCNT_CONSTEXPR(Sched_Clock / NTR_SysClock) == 1, "this code no longer works, mate\n");
        constexpr u8 sched_timershift = CTZ_CONSTEXPR(Sched_Clock / NTR_SysClock);

        if (type == TimerType_Snd) timer->DividerShift = 1 + sched_timershift;
        else if (timer->CR.OverflowTick) timer->DividerShift = 0;
        else timer->DividerShift = ((timer->CR.Divider == 0) ? 0 : ((timer->CR.Divider * 2) + 4)) + sched_timershift;

        if (!oldenable && timer->CR.Enable)
        {
            timer->On = true;
            if (type == TimerType_Snd && timer->Counter == 0xFFFF) LogPrint(LOG_BUG, "Sound timer enable at overflow?\n");
            timer->NeedsEnable = true; // loading cr is delayed by 1 cycle

            Scheduler_Events evt;
            switch(type)
            {
            case TimerType_7:   evt = Evt_Timer70CR; break;
            case TimerType_9:   evt = Evt_Timer90CR; break;
            case TimerType_Snd: evt = Evt_TimerSnd0CR; break;
            }
            Sched_AddEvent(sys, now+DSClk33(1), evt+num);
        }
        else if (oldenable && !timer->CR.Enable) timer->On = false; // disable is not delayed by 1 cycle...?
        else
        {
            Timer_CalcNextEvent(sys, arr, num, now, type);
        }
    }
    timer->LastUpdated = now;
}

void Timer_IOWriteHandler(Console* sys, const timestamp now, const u32 addr, const u32 val, const u32 mask, const bool a9)
{
    u8 num = ((addr & 0xC) / 4) % 4;
    Timer* timer = &(a9 ? sys->Timers9 : sys->Timers7)[num];

    u32 mask2 = ((num == 0) ? 0xC3'FFFF : 0xC7'FFFF);
    MaskedWrite(timer->BufferedRegs, val, mask & mask2);

    timer->NeedsUpdate = true;

    Sched_AddEvent(sys, now+DSClk33(1), (a9 ? Evt_Timer90CR : Evt_Timer70CR) + num);
}

u32 Timer_IOReadHandler(Console* sys, const timestamp now, const u32 addr, const bool a9)
{
    u8 num = ((addr & 0xC) / 4) % 4;
    Timer* timer = &(a9 ? sys->Timers9 : sys->Timers7)[num];

    Timer_Run(sys, (a9 ? sys->Timers9 : sys->Timers7), num, now, a9 ? TimerType_9 : TimerType_7);

    return timer->CR.Raw << 16 | timer->Counter;
}
