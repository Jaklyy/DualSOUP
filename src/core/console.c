#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "console.h"
#include "arm/arm9/arm.h"
#include "dma/dma.h"
#include "scheduler.h"
#include "utils.h"
#include "video.h"
#include "bus/io.h"




// TODO: this function probably shouldn't manage memory on its own?
struct Console* Console_Init(struct Console* sys, FILE* ntr9, FILE* ntr7)
{
    if (sys == nullptr)
    {
        // allocate and initialize 
        sys = aligned_alloc(alignof(struct Console), sizeof(struct Console));

        if (sys == NULL)
        {
            LogPrint(LOG_ALWAYS, "FATAL: Memory allocation failed.\n");
            return nullptr;
        }
    }
    else
    {
        // de-allocate coroutine handles
        CR_Free(sys->HandleARM9);
        CR_Free(sys->HandleARM7);
    }

    // wipe entire emulator state
    memset(sys, 0, sizeof(*sys));
    ARM9_Init(&sys->ARM9, sys);
    ARM7_Init(&sys->ARM7, sys);

    // initialize coroutine handles
    sys->HandleMain = CR_Active();

    if (sys->HandleMain == cr_null)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle allocation failed.\n");
        free(sys); // probably a good idea to not leak memory, just in case.
        return nullptr;
    }

    sys->HandleARM9 = CR_Create((void*)ARM9_MainLoop, &sys->ARM9);

    if (sys->HandleARM9 == cr_null)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle allocation failed.\n");
        free(sys); // probably a good idea to not leak memory, just in case.
        return nullptr;
    }

    sys->HandleARM7 = CR_Create((void*)ARM7_MainLoop, &sys->ARM7);

    if (sys->HandleARM7 == cr_null)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle allocation failed.\n");
        free(sys); // probably a good idea to not leak memory, just in case.
        return nullptr;
    }

    int num; 
    // allocate any internal or external ROMs
    if (ntr9 != NULL)
    {
        num = fread(sys->NTRBios9.b8, NTRBios9_Size, 1, ntr9);

        if (num != 1)
        {
            LogPrint(LOG_ALWAYS, "ERROR: ARM9 BIOS did not load properly. This will cause issues!!!\n");
            // TODO: this should probably return an error code to the frontend.
            exit(EXIT_FAILURE);
        }
    }

    if (ntr7 != NULL)
    {
        num = fread(sys->NTRBios7.b8, NTRBios7_Size, 1, ntr7);

        if (num != 1)
        {
            LogPrint(LOG_ALWAYS, "ERROR: ARM7 BIOS did not load properly. This will cause issues!!!\n");
            // TODO: this should probably return an error code to the frontend.
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < Sched_MAX; i++)
        sys->Sched.EventTimes[i] = timestamp_max;


    for (int i = 0; i < IRQ_Max; i++)
        sys->IRQSched9[i] = timestamp_max;

    for (int i = 0; i < IRQ_Max; i++)
        sys->IRQSched7[i] = timestamp_max;

    // TODO: is this always running?
    Schedule_Event(sys, LCD_Scanline, Sched_Scanline, 0);

    sys->DMA9.ChannelTimestamps[0] = timestamp_max;
    sys->DMA9.ChannelTimestamps[1] = timestamp_max;
    sys->DMA9.ChannelTimestamps[2] = timestamp_max;
    sys->DMA9.ChannelTimestamps[3] = timestamp_max;

    sys->DMA7.ChannelTimestamps[0] = timestamp_max;
    sys->DMA7.ChannelTimestamps[1] = timestamp_max;
    sys->DMA7.ChannelTimestamps[2] = timestamp_max;
    sys->DMA7.ChannelTimestamps[3] = timestamp_max;

    Console_Reset(sys);

    return sys;
}

void Console_DirectBoot(struct Console* sys, FILE* rom)
{
    // wram should probably be enabled...?
    sys->IO.WRAMCR = 3;

    // set main ram bits to be enabled
    sys->IO.ExtMemCR_Shared.MRSomething1 = true;
    sys->IO.ExtMemCR_Shared.MRSomething2 = true;
    sys->IO.ExtMemCR_Shared.MRPriority = true; // ARM7

    fseek(rom, 0x20, SEEK_SET);
    u32 vars[8];

    fread(vars, 4*4*2, 1, rom);

    fseek(rom, vars[0], SEEK_SET);
    {
        if (vars[3] > MiB(512)) return;
    timestamp nop;
    u32* arry = malloc(vars[3]);

    fseek(rom, vars[0], SEEK_SET);
    fread(arry, vars[3], 1, rom);
    bool nopy;

    for (unsigned i = 0; i < vars[3]/4; i++)
    {
        AHB9_Write(sys, &nop, vars[2], arry[i], 0xFFFFFFFF, false, &nopy, false);
        vars[2]+=4;
    }
    free(arry);
    }
    {
        if (vars[7] > MiB(512)) return;
    timestamp nop;
    u32* arry = malloc(vars[7]);

    fseek(rom, vars[4], SEEK_SET);
    fread(arry, vars[7], 1, rom);
    bool nopy;

    for (unsigned i = 0; i < vars[7]/4; i++)
    {
        AHB7_Write(sys, &nop, vars[6], arry[i], 0xFFFFFFFF, false, &nopy, false);
        vars[6]+=4;
    }
    free(arry);
    }

    memset(&sys->AHB7, 0, sizeof(sys->AHB7));
    memset(&sys->AHB9, 0, sizeof(sys->AHB9));
    memset(&sys->BusMR, 0, sizeof(sys->BusMR));


    ARM9_SetPC(&sys->ARM9, vars[1], 0);
    ARM7_SetPC(&sys->ARM7, vars[5]);
}

void Console_Reset(struct Console* sys)
{
    ARM9_Reset(&sys->ARM9, false /*unverified I guess?*/, true);
    ARM7_Reset(&sys->ARM7);

    // TODO: reset dma?
}

void Console_MainLoop(struct Console* sys)
{
    Scheduler_UpdateTargets(sys);
    while(true)
    {
        CR_Switch(sys->HandleARM9);
        CR_Switch(sys->HandleARM7);

        Scheduler_Run(sys);
    }
}

timestamp Console_GetARM7Cur(struct Console* sys)
{
    timestamp ts = sys->ARM7.ARM.Timestamp;
    if (ts < sys->AHB7.Timestamp)
        ts = sys->AHB7.Timestamp;
    if ((ts < sys->Sched.EventTimes[Sched_DMA7]) && (sys->Sched.EventTimes[Sched_DMA7] != timestamp_max))
        ts = sys->Sched.EventTimes[Sched_DMA7];

    return ts;
}

timestamp Console_GetARM9Cur(struct Console* sys)
{
    timestamp ts = sys->ARM9.ARM.Timestamp/2;

    if (ts < sys->AHB9.Timestamp)
        ts = sys->AHB9.Timestamp;

    if ((ts < sys->Sched.EventTimes[Sched_DMA9]) && (sys->Sched.EventTimes[Sched_DMA9] != timestamp_max))
        ts = sys->Sched.EventTimes[Sched_DMA9];
    return ts;
}

void IF9_Update(struct Console* sys, timestamp now)
{
    timestamp time = sys->Sched.EventTimes[Sched_IF9Update];
    timestamp next = timestamp_max;
    for (int i = 0; i < IRQ_Max; i++)
    {
        if (time >= sys->IRQSched9[i])
        {
            sys->IO.IF9 |= (1<<i);
            sys->IRQSched9[i] = timestamp_max;
        }
        if (next > sys->IRQSched9[i])
            next = sys->IRQSched9[i];
    }
    Schedule_Event(sys, IF9_Update, Sched_IF9Update, next);
}

void IF7_Update(struct Console* sys, timestamp now)
{
    timestamp time = sys->Sched.EventTimes[Sched_IF7Update];
    timestamp next = timestamp_max;
    for (int i = 0; i < IRQ_Max; i++)
    {
        if (time >= sys->IRQSched7[i])
        {
            sys->IO.IF7 |= (1<<i);
            sys->IRQSched7[i] = timestamp_max;
        }
        if (next > sys->IRQSched7[i])
            next = sys->IRQSched7[i];
    }
    Schedule_Event(sys, IF7_Update, Sched_IF7Update, next);
}

void Console_ScheduleIRQs(struct Console* sys, const u8 irq, const bool a9, timestamp time)
{
    timestamp* irqs;
    if (a9)
    {
        irqs = sys->IRQSched9;
    }
    else
    {
        irqs = sys->IRQSched7;
    }

    irqs[irq] = time;

    timestamp next = timestamp_max;

    for (int i = 0; i < IRQ_Max; i++)
    {
        if (next > irqs[i])
            next = irqs[i];
    }

    if (a9)
        Schedule_Event(sys, IF9_Update, Sched_IF9Update, time);
    else
        Schedule_Event(sys, IF7_Update, Sched_IF7Update, time);
}
