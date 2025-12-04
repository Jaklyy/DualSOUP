#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <threads.h>
#include "console.h"
#include "arm/arm9/arm.h"
#include "arm/shared/arm.h"
#include "io/dma.h"
#include "scheduler.h"
#include "utils.h"
#include "video/video.h"
#include "bus/io.h"
#include "sram/flash.h"




// TODO: this function probably shouldn't manage memory on its own?
struct Console* Console_Init(struct Console* sys, FILE* ntr9, FILE* ntr7, FILE* firmware, FILE* rom, void* pad)
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
        // de-allocate shit so it can be re-allocated
        // TODO: dont do this?
        CR_Free(sys->HandleARM9);
        CR_Free(sys->HandleARM7);
        mtx_destroy(&sys->FrameBufferMutex);
        mtx_destroy(&sys->Sched.SchedulerMtx);
        Flash_Cleanup(&sys->Firmware);
        Gamecard_Cleanup(&sys->Gamecard);
    }

    // wipe entire emulator state
    memset(sys, 0, sizeof(*sys));
    CR_Start = false;

    // allocate shit
    bool cr7init = CR_Create(&sys->HandleARM9, (void*)ARM9_MainLoop, &sys->ARM9);
    bool cr9init = CR_Create(&sys->HandleARM7, (void*)ARM7_MainLoop, &sys->ARM7);
    bool firminit = Flash_Init(&sys->Firmware, firmware, true);
    bool gcinit = Gamecard_Init(&sys->Gamecard, rom);
    sys->HandleMain = CR_Active();

    int num9;
    if (ntr9 != NULL) num9 = fread(sys->NTRBios9.b8, NTRBios9_Size, 1, ntr9);
    int num7;
    if (ntr7 != NULL) num7 = fread(sys->NTRBios7.b8, NTRBios7_Size, 1, ntr7);

    bool mtxinit = (mtx_init(&sys->FrameBufferMutex, mtx_plain) == thrd_success);
    bool mtxinit2 = (mtx_init(&sys->Sched.SchedulerMtx, mtx_recursive) == thrd_success);

    if ((!cr7init) || (!cr9init)|| (num9 != 1) || (num7 != 1) || !firminit || !gcinit || !mtxinit || !mtxinit2)
    {
        // return error messages
        if ((!cr7init) || (!cr9init))
            LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle creation failed:%s%s\n", ( cr7init ? " 7": ""), ( cr9init ? " 9": ""));
        if (!mtxinit || !mtxinit2)
            LogPrint(LOG_ALWAYS, "FATAL: Mutex init failed.\n");
        if (num9 != 1)
            LogPrint(LOG_ALWAYS, "FATAL: ARM9 BIOS did not load properly.\n");
        if (num7 != 1)
            LogPrint(LOG_ALWAYS, "FATAL: ARM7 BIOS did not load properly.\n");

        // cleanup ones that actually allocated correctly
        CR_Free(sys->HandleARM9);
        CR_Free(sys->HandleARM7);
        Flash_Cleanup(&sys->Firmware);
        Gamecard_Cleanup(&sys->Gamecard);
        if (mtxinit) mtx_destroy(&sys->FrameBufferMutex);
        if (mtxinit2) mtx_destroy(&sys->Sched.SchedulerMtx);
        free(sys);

        return nullptr;
    }

    // init variables

    ARM9_Init(&sys->ARM9, sys);
    ARM7_Init(&sys->ARM7, sys);

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

    sys->IPCFIFO7.CR.RecvFIFOEmpty = true;
    sys->IPCFIFO7.CR.SendFIFOEmpty = true;
    sys->IPCFIFO9.CR.RecvFIFOEmpty = true;
    sys->IPCFIFO9.CR.SendFIFOEmpty = true;

    sys->Pad = pad;

    // run power on/reset logic
    Console_Reset(sys);

    return sys;
}

void Console_DirectBoot(struct Console* sys, FILE* rom)
{
    // wram should probably be enabled...?
    sys->WRAMCR = 3;
    sys->PostFlag = true;
    sys->PowerCR9.Raw = 0x820F;

    sys->Gamecard.Mode = Key2;

    // set main ram bits to be enabled
    sys->ExtMemCR_Shared.MRSomething1 = true;
    sys->ExtMemCR_Shared.MRSomething2 = true;
    sys->ExtMemCR_Shared.MRPriority = true; // ARM7
    sys->ExtMemCR_Shared.GBAPakAccess = true;
    sys->ExtMemCR_Shared.NDSCardAccess = true;

    ARM_SetMode((struct ARM*)&sys->ARM9, ARMMode_SYS);
    ARM_SetMode((struct ARM*)&sys->ARM7, ARMMode_SYS);
    sys->ARM9.ARM.SP = 0x03002F7C;
    sys->ARM9.ARM.IRQ_Bank.R[0] = 0x03003F80;
    sys->ARM9.ARM.SWI_Bank.R[0] = 0x03003FC0;
    sys->ARM7.ARM.SP = 0x0380FD80;
    sys->ARM7.ARM.IRQ_Bank.R[0] = 0x0380FF80;
    sys->ARM7.ARM.SWI_Bank.R[0] = 0x0380FFC0;

    fseek(rom, 0x20, SEEK_SET);
    u32 vars[8];

    fread(vars, 4*4*2, 1, rom);

    sys->ARM9.ARM.R12 = vars[1];
    sys->ARM9.ARM.R14 = vars[1];
    sys->ARM7.ARM.R12 = vars[5];
    sys->ARM7.ARM.R14 = vars[5];

    fseek(rom, vars[0], SEEK_SET);

    // load arm9 rom
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

    // load arm7 rom
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

    sys->ARM9.CP15.CR.DTCMEnable = true;
    sys->ARM9.CP15.DTCMCR.Raw = 0x0300000A;
    ARM9_ConfigureDTCM(&sys->ARM9);

    // load header
    fseek(rom, 0, SEEK_SET);
    fread(&sys->MainRAM.b8[0x27FFE00 & (MainRAM_Size-1)], 0x170*sizeof(u32), 1, rom);

    // "load" chipid
    sys->MainRAM.b32[(0x27FF800 & (MainRAM_Size-1))/sizeof(u32)] = 0x01010101;
    sys->MainRAM.b32[(0x27FF804 & (MainRAM_Size-1))/sizeof(u32)] = 0x01010101;
    sys->MainRAM.b32[(0x27FFC00 & (MainRAM_Size-1))/sizeof(u32)] = 0x01010101;
    sys->MainRAM.b32[(0x27FFC04 & (MainRAM_Size-1))/sizeof(u32)] = 0x01010101;

    // header checksum
    fseek(rom, 0x15E, SEEK_SET);
    fread(&sys->MainRAM.b8[0x27FF808 & (MainRAM_Size-1)], 2, 1, rom);
    fseek(rom, 0x15E, SEEK_SET);
    fread(&sys->MainRAM.b8[0x27FFC08 & (MainRAM_Size-1)], 2, 1, rom);
    // secure area checksum
    fseek(rom, 0x15E, SEEK_SET);
    fread(&sys->MainRAM.b8[0x27FF80A & (MainRAM_Size-1)], 2, 1, rom);
    fseek(rom, 0x15E, SEEK_SET);
    fread(&sys->MainRAM.b8[0x27FFC0A & (MainRAM_Size-1)], 2, 1, rom);

    // idk
    sys->MainRAM.b16[(0x27FF850 & (MainRAM_Size-1))/sizeof(u16)] = 0x5835;
    sys->MainRAM.b16[(0x27FFC10 & (MainRAM_Size-1))/sizeof(u16)] = 0x5835;
    sys->MainRAM.b16[(0x27FFC30 & (MainRAM_Size-1))/sizeof(u16)] = 0xFFFF;
    sys->MainRAM.b16[(0x27FFC40 & (MainRAM_Size-1))/sizeof(u16)] = 0x0001;

    u16 usersettings = (sys->Firmware.RAM[0x20] | (sys->Firmware.RAM[0x21] << 8))*8;

    sys->MainRAM.b32[((0x27FF864) & (MainRAM_Size-1))/4] = 0;
    sys->MainRAM.b32[((0x27FF868) & (MainRAM_Size-1))/4] = usersettings;

    sys->MainRAM.b16[((0x27FF874) & (MainRAM_Size-1))/2] = (sys->Firmware.RAM[0x26] | (sys->Firmware.RAM[0x27] << 8));

    sys->MainRAM.b16[((0x27FF876) & (MainRAM_Size-1))/2] = (sys->Firmware.RAM[4] | (sys->Firmware.RAM[5] << 8));

    for (int i = 0; i < (0x70*4); i++)
        sys->MainRAM.b8[((0x27FFC80 + i) & (MainRAM_Size-1))] = sys->Firmware.RAM[usersettings+i];


    ARM9_SetPC(&sys->ARM9, vars[1], 0);
    ARM7_SetPC(&sys->ARM7, vars[5]);
}

void Console_Reset(struct Console* sys)
{
    ARM9_Reset(&sys->ARM9, false /*unverified I guess?*/, true);
    ARM7_Reset(&sys->ARM7);

    // TODO: reset dma?
}

timestamp Console_GetARM7Cur(struct Console* sys)
{
    if (sys->ARM7.ARM.DeadAsleep) return timestamp_max;

    timestamp ts = sys->ARM7.ARM.Timestamp;

    if (ts < sys->AHB7.Timestamp)
        ts = sys->AHB7.Timestamp;

    if ((ts < sys->Sched.EventTimes[Sched_DMA7]) && (sys->Sched.EventTimes[Sched_DMA7] != timestamp_max))
        ts = sys->Sched.EventTimes[Sched_DMA7];

    return ts;
}

timestamp Console_GetARM9Cur(struct Console* sys)
{
    if (sys->ARM9.ARM.DeadAsleep) return timestamp_max;

    timestamp ts = sys->ARM9.ARM.Timestamp >> ((sys->ARM9.BoostedClock) ? 2 : 1);

    if (ts < sys->AHB9.Timestamp)
        ts = sys->AHB9.Timestamp;

    if ((ts < sys->Sched.EventTimes[Sched_DMA9]) && (sys->Sched.EventTimes[Sched_DMA9] != timestamp_max))
        ts = sys->Sched.EventTimes[Sched_DMA9];

    return ts;
}

void Console_SyncWith7GTE(struct Console* sys, timestamp now)
{
    while(now >= Console_GetARM7Cur(sys))
    {
        CR_Switch(sys->HandleARM7);
    }
}

void Console_SyncWith7GT(struct Console* sys, timestamp now)
{
    while(now > Console_GetARM7Cur(sys))
    {
        CR_Switch(sys->HandleARM7);
    }
}

void Console_SyncWith9GTE(struct Console* sys, timestamp now)
{
    while(now >= Console_GetARM9Cur(sys))
    {
        CR_Switch(sys->HandleARM9);
    }
}

void Console_SyncWith9GT(struct Console* sys, timestamp now)
{
    while(now > Console_GetARM9Cur(sys))
    {
        CR_Switch(sys->HandleARM9);
    }
}

void IF9_Update(struct Console* sys, timestamp now)
{
    timestamp time = sys->Sched.EventTimes[Sched_IF9Update];
    timestamp next = timestamp_max;
    for (int i = 0; i < IRQ_Max; i++)
    {
        if (time >= sys->IRQSched9[i])
        {
            sys->IF9 |= (1<<i);
            sys->IRQSched9[i] = timestamp_max;
        }
        if (next > sys->IRQSched9[i])
            next = sys->IRQSched9[i];
    }

    // wake up cpu
    // TODO: SCHEDULE THIS INSTEAD
    if (sys->ARM9.ARM.CpuSleeping && sys->IME9 && (sys->IE9 & sys->IF9))
    {
        sys->ARM9.ARM.CpuSleeping = 0;
        sys->ARM9.ARM.Timestamp = now << ((sys->ARM9.BoostedClock) ? 2 : 1);
        ARM9_ExecuteCycles(&sys->ARM9, 1, 1);
        sys->ARM9.ARM.CodeSeq = false;
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
            sys->IF7 |= (1<<i);
            sys->IRQSched7[i] = timestamp_max;
        }
        if (next > sys->IRQSched7[i])
            next = sys->IRQSched7[i];
    }
    // wake up cpu
    // TODO: SCHEDULE THIS INSTEAD
    if (sys->ARM7.ARM.CpuSleeping && (sys->IE7 & sys->IF7))
    {
        sys->ARM7.ARM.CpuSleeping = 0;
        sys->ARM7.ARM.Timestamp = now;
        ARM7_ExecuteCycles(&sys->ARM7, 1);
        sys->ARM7.ARM.CodeSeq = false;
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

void Console_MainLoop(struct Console* sys)
{
    CR_Start = true;
    Scheduler_UpdateTargets(sys);
    while(true)
    {
        if (sys->KillThread)
        {
            return;
        }

#ifdef UseThreads
        while (Console_GetARM9Cur(sys) < sys->ARM7Target) ;//printf("9 %li %li 7 %li %li\n", sys->ARM9.ARM.Timestamp, sys->ARM9Target, sys->ARM7.ARM.Timestamp, sys->ARM7Target);
        while (Console_GetARM7Cur(sys) < sys->ARM7Target) ;//printf("7 %li %li 9 %li %li\n", sys->ARM7.ARM.Timestamp, sys->ARM7Target, sys->ARM9.ARM.Timestamp, sys->ARM9Target);
#else

        while((Console_GetARM7Cur(sys) < sys->ARM7Target) || (Console_GetARM9Cur(sys) < sys->ARM7Target))
        {
            if (!sys->ARM9.ARM.DeadAsleep && (Console_GetARM9Cur(sys) < sys->ARM7Target))
                CR_Switch(sys->HandleARM9);
            if (!sys->ARM7.ARM.DeadAsleep && (Console_GetARM7Cur(sys) < sys->ARM7Target))
                CR_Switch(sys->HandleARM7);
        }
        //printf("9 %lu %lu\n", Console_GetARM9Cur(sys), sys->ARM7Target);
        //printf("7 %lu %lu\n", Console_GetARM7Cur(sys), sys->ARM7Target);
#endif
        Scheduler_Run(sys);
    }
    return;
}
