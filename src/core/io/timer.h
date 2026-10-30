#pragma once
#include "core/utils.h"



typedef enum
{
    TimerType_7,
    TimerType_9,
    TimerType_Snd,
} TimerType;

typedef struct
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
    u32 BufferedRegs;
} Timer;

typedef struct Console Console;

void Timer_Run(Console* sys, Timer arr[], u8 num, timestamp now, TimerType type);
void Timer_UpdateCR(Console* sys, Timer arr[], u8 num, timestamp now, TimerType type);
void Timer_IOWriteHandler(Console* sys, const timestamp now, const u32 addr, u32 val, const u32 mask, const bool a9);
u32 Timer_IOReadHandler(Console* sys, const timestamp now, const u32 addr, const bool a9);