#pragma once
#include "core/utils.h"




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
                    bool OverflowTick : 1;
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

//temp
enum TIMERTEMP
{
    TIMER_UPDATECR,
    TIMER_SCHEDRUN,
};

typedef struct Console Console;

void Timer9_UpdateCRs(Console* sys, timestamp now);
void Timer7_UpdateCRs(Console* sys, timestamp now);
void Timer_SchedRun9(Console* sys, timestamp now);
void Timer_SchedRun7(Console* sys, timestamp now);

void Timer_CalcNextIRQ(Console* sys, timestamp now, bool a9);
void Timer_IOWriteHandler(Console* sys, const timestamp curts, const u32 addr, u32 val, const u32 mask, const bool a9);
u32 Timer_IOReadHandler(Console* sys, const timestamp curts, const u32 addr, const bool a9);