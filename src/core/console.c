#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_gamepad.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <threads.h>
#include "console.h"
#include "arm/arm9/arm.h"
#include "arm/shared/arm.h"
#include "io/dma.h"
#include "irq.h"
#include "scheduler.h"
#include "utils.h"
#include "video/3d.h"
#include "video/ppu.h"
#include "video/video.h"
#include "sram/flash.h"



void Console_DebugLog(struct Console* sys)
{
    if (!SDL_GetGamepadButton(sys->Pad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) return;

#if 1
    for (int i = 0; i < 16; i++)
    {
        printf("channel %i: dmacr:%08X dmats:%08lX dmasa:%08X dmaln%08X dmamd%i\nchraw: %08X cen:%i cfm:%i crm:%i chln%i chlp%i chpr:%08X chmx:%08lX\ntimercr:%06X fifd:%i fiff:%i fifs:%i\n", i, \
        sys->DMA7.Channels[i].CR.Raw, sys->DMA7.ChannelTimestamps[i], sys->DMA7.Channels[i].Latched_SrcAddr, sys->DMA7.Channels[i].Latched_NumWords, sys->DMA7.Channels[i].CurrentMode, \
        sys->SoundChannels[i].CR.Raw, sys->SoundChannels[i].CR.Enable, sys->SoundChannels[i].CR.Format, sys->SoundChannels[i].CR.RepeatMode, sys->SoundChannels[i].SoundLen, sys->SoundChannels[i].LoopOffs, \
        sys->SoundChannels[i].Prog, sys->SoundChannels[i].SampleMax, \
        sys->Timers7[i+4].Regs, sys->SoundChannels[i].FIFO_DrainPtr, sys->SoundChannels[i].FIFO_FillPtr, sys->SoundChannels[i].FIFO_Bytes);
    }
    printf("dma cur: %08X\n", sys->DMA7.CurMask);
#elif
    bool seq = false;
    printf("dumping\n");
    {
        FILE* file = fopen("log7.bin", "wb");
        for (int i = 0x02000000; i < 0x08000000; i+=4)
        {
            u32 buf = AHB7_Read(sys, NULL, i, 0xFFFFFFFF, false, false, &seq, false, 0);
            fwrite(&buf, 4, 1, file);
        }
        fclose(file);
    }
    {
        FILE* file = fopen("log9dt.bin", "wb");
        fwrite(sys->ARM9.DTCM.b8, ARM9_DTCMSize, 1, file);
        fclose(file);
    }
    {
        FILE* file = fopen("log9it.bin", "wb");
        fwrite(sys->ARM9.ITCM.b8, ARM9_ITCMSize, 1, file);
        fclose(file);
    }
    {
        FILE* file = fopen("log9.bin", "wb");
        for (int i = 0x02000000; i < 0x08000000; i+=4)
        {
            u32 buf = AHB9_Read(sys, NULL, i, 0xFFFFFFFF, false, false, &seq, false);
            fwrite(&buf, 4, 1, file);
        }
        fclose(file);
    }
    printf("done\n");
    while (SDL_GetGamepadButton(sys->Pad, SDL_GAMEPAD_BUTTON_LEFT_STICK));
#endif
}

// TODO: this function probably shouldn't manage memory on its own?
struct Console* Console_Init(struct Console* sys, FILE* ntr9, FILE* ntr7, FILE* firmware, const char* rom, void* pad, void* aud)
{
    u8* nvram = nullptr;
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
        mtx_destroy(&sys->FrameBufferMutex[0]);
        mtx_destroy(&sys->FrameBufferMutex[1]);
#ifdef UseThreads
        mtx_destroy(&sys->Sched.SchedulerMtx);
#endif
        //Flash_Cleanup(&sys->Firmware);
        nvram = sys->Firmware.RAM;
        Gamecard_Cleanup(&sys->Gamecard);
        int dummy;
        sys->KillSWRen = true;
        sys->SWRenStart = true;
        sys->SWRenTarget = timestamp_max;
        sys->RenderedLines = 255;
#ifndef PPUST
        sys->KillPPUs = true;
        sys->PPUStart = true;
        sys->PPUTarget = timestamp_max;
        thrd_join(sys->PPUAThread, &dummy);
        thrd_join(sys->PPUBThread, &dummy);
#endif
        thrd_join(sys->SWRenThread, &dummy);
    }

    // wipe entire emulator state
    memset(sys, 0, sizeof(*sys));
    CR_Start = false;

    sys->ConsoleModel = MODEL_USG; // TODO: make configurable.

    int num9 = 0;
    if (ntr9 != NULL) num9 = fread(sys->NTRBios9.b8, NTRBios9_Size, 1, ntr9);
    int num7 = 0;
    if (ntr7 != NULL) num7 = fread(sys->NTRBios7.b8, NTRBios7_Size, 1, ntr7);

    bool firminit = false;
    fseek(firmware, 0, SEEK_END);
    u64 nvramsize = ftell(firmware);
    fseek(firmware, 0, SEEK_SET);
    if (nvram == nullptr)
    {
        nvram = malloc(nvramsize);
        if (nvram != NULL)
        {
            firminit = fread(nvram, nvramsize, 1, firmware) != 0;
            Flash_Init(&sys->Firmware, nvram, nvramsize, true, 0x010101);
        }
    }
    else
    {
        firminit = true;
        Flash_Init(&sys->Firmware, nvram, nvramsize, true, 0x010101);
    }

    // allocate shit
    bool cr7init = CR_Create(&sys->HandleARM9, (void*)ARM9_MainLoop, &sys->ARM9);
    bool cr9init = CR_Create(&sys->HandleARM7, (void*)ARM7_MainLoop, &sys->ARM7);

    bool gcinit = Gamecard_Init(&sys->Gamecard, rom, sys->NTRBios7.b8);
    sys->HandleMain = CR_Active();

    bool mtxinit = (mtx_init(&sys->FrameBufferMutex[0], mtx_plain) == thrd_success);
    bool mtxinit3 = (mtx_init(&sys->FrameBufferMutex[1], mtx_plain) == thrd_success);
#ifdef UseThreads
    bool mtxinit2 = (mtx_init(&sys->Sched.SchedulerMtx, mtx_recursive) == thrd_success);
#else
    bool mtxinit2 = true;
#endif
#ifndef PPUST
    bool thrdinit1 = (thrd_create(&sys->PPUAThread, PPUA_MainLoop, sys) == thrd_success);
    bool thrdinit2 = (thrd_create(&sys->PPUBThread, PPUB_MainLoop, sys) == thrd_success);
#else
    bool thrdinit1 = true, thrdinit2 = true;
#endif
    bool thrdinit3 = (thrd_create(&sys->SWRenThread, SWRen_MainLoop, sys) == thrd_success);

    if ((!cr7init) || (!cr9init)|| (num9 != 1) || (num7 != 1) || !firminit || !gcinit || !mtxinit || !mtxinit2|| !mtxinit3 || !thrdinit1 || !thrdinit2)
    {
        // return error messages
        if ((!cr7init) || (!cr9init))
            LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle creation failed:%s%s\n", ( cr7init ? " 7": ""), ( cr9init ? " 9": ""));
        if (!mtxinit || !mtxinit2|| !mtxinit3)
            LogPrint(LOG_ALWAYS, "FATAL: Mutex init failed.\n");
        if (num9 != 1)
            LogPrint(LOG_ALWAYS, "FATAL: ARM9 BIOS did not load properly.\n");
        if (num7 != 1)
            LogPrint(LOG_ALWAYS, "FATAL: ARM7 BIOS did not load properly.\n");
        if (!thrdinit1 || !thrdinit2)
            LogPrint(LOG_ALWAYS, "FATAL: PPU Thread creation failed.\n");
        if (!thrdinit3)
            LogPrint(LOG_ALWAYS, "FATAL: 3D Rasterizer Thread creation failed.\n");

        if (!gcinit)
        {
            LogPrint(LOG_ALWAYS, "FATAL: Gamecard failed init.\n");
        }

        // cleanup ones that actually allocated correctly
        if (cr9init) CR_Free(sys->HandleARM9);
        if (cr7init) CR_Free(sys->HandleARM7);
        if (firminit) Flash_Cleanup(&sys->Firmware);
        if (gcinit) Gamecard_Cleanup(&sys->Gamecard);
        if (mtxinit) mtx_destroy(&sys->FrameBufferMutex[0]);
        if (mtxinit3) mtx_destroy(&sys->FrameBufferMutex[1]);
#ifdef UseThreads
        if (mtxinit2) mtx_destroy(&sys->Sched.SchedulerMtx);
#endif
        int dummy;
        sys->KillSWRen = true;
        sys->SWRenStart = true;
        sys->SWRenTarget = timestamp_max;
        sys->RenderedLines = 255;
#ifndef PPUST
        sys->KillPPUs = true;
        sys->PPUStart = true;
        sys->PPUTarget = timestamp_max;
        thrd_join(sys->PPUAThread, &dummy);
        thrd_join(sys->PPUBThread, &dummy);
#endif
        thrd_join(sys->SWRenThread, &dummy);

        free(sys);
        sys = nullptr;

        return nullptr;
    }

    sys->Pad = pad;
    sys->Aud = aud;

    // init variables

    ARM9_Init(&sys->ARM9, sys);
    ARM7_Init(&sys->ARM7, sys);

    for (int i = 0; i < Evt_Max; i++)
        sys->Sched.EventTimes[i] = timestamp_max;

    for (int i = 0; i < IRQ_Max; i++)
        sys->IRQSched9[i] = timestamp_max;

    for (int i = 0; i < IRQ_Max; i++)
        sys->IRQSched7[i] = timestamp_max;

    for (unsigned i = 0; i < (sizeof(sys->DMA9.ChannelTimestamps) / sizeof(sys->DMA9.ChannelTimestamps[0])); i++)
    {
        sys->DMA9.ChannelTimestamps[i] = timestamp_max;
        sys->DMA7.ChannelTimestamps[i] = timestamp_max;
    }

    sys->DMA9.NextTime = timestamp_max;
    sys->DMA7.NextTime = timestamp_max;

    for (int i = DMA7_SoundBase; i < DMA7_SoundMax; i++)
    {
        sys->DMA7.Channels[i].CurrentMode = DMAStart_Audio;
        sys->DMA7.Channels[i].CR.Width32 = true;
        sys->DMA7.Channels[i].SrcInc = 4;
        sys->DMA7.Channels[i].SrcAddrMask = 0x07FFFFFC;
    }
    for (int i = DMA7_SoundCapBase; i < DMA7_SoundCapMax; i++)
    {
        sys->DMA7.Channels[i].CurrentMode = DMAStart_AudioCap;
        sys->DMA7.Channels[i].CR.Width32 = true;
        sys->DMA7.Channels[i].DstInc = 4;
        sys->DMA7.Channels[i].CR.DestCR = 3;
        sys->DMA7.Channels[i].DstAddrMask = 0x07FFFFFC;
        sys->DMA7.Channels[i].CR.Enable = true;
        sys->DMA7.Channels[i].CR.Repeat = true;
    }

    sys->DMA7.Channels[0+DMA7_NormalBase].SrcAddrMask = 0x07FFFFFE;
    sys->DMA7.Channels[1+DMA7_NormalBase].SrcAddrMask = 0x0FFFFFFE;
    sys->DMA7.Channels[2+DMA7_NormalBase].SrcAddrMask = 0x0FFFFFFE;
    sys->DMA7.Channels[3+DMA7_NormalBase].SrcAddrMask = 0x0FFFFFFE;

    sys->DMA7.Channels[0+DMA7_NormalBase].DstAddrMask = 0x0FFFFFFE;
    sys->DMA7.Channels[1+DMA7_NormalBase].DstAddrMask = 0x0FFFFFFE;
    sys->DMA7.Channels[2+DMA7_NormalBase].DstAddrMask = 0x0FFFFFFE;
    sys->DMA7.Channels[3+DMA7_NormalBase].DstAddrMask = 0x07FFFFFE;

    sys->DMA9.Channels[0].SrcAddrMask = 0x0FFFFFFE;
    sys->DMA9.Channels[1].SrcAddrMask = 0x0FFFFFFE;
    sys->DMA9.Channels[2].SrcAddrMask = 0x0FFFFFFE;
    sys->DMA9.Channels[3].SrcAddrMask = 0x0FFFFFFE;

    sys->DMA9.Channels[0].DstAddrMask = 0x0FFFFFFE;
    sys->DMA9.Channels[1].DstAddrMask = 0x0FFFFFFE;
    sys->DMA9.Channels[2].DstAddrMask = 0x0FFFFFFE;
    sys->DMA9.Channels[3].DstAddrMask = 0x0FFFFFFE;

    sys->DMA9.CurMask = 0xFFFFFFF'0;
    sys->DMA7.CurMask = u32_max << DMA7_Max;
    sys->DMA9.NextID = 31;
    sys->DMA7.NextID = 31;

    sys->IPCFIFO7.CR.RecvFIFOEmpty = true;
    sys->IPCFIFO7.CR.SendFIFOEmpty = true;
    sys->IPCFIFO9.CR.RecvFIFOEmpty = true;
    sys->IPCFIFO9.CR.SendFIFOEmpty = true;

    sys->Powman.BacklightLevels.AlwaysSet = true;

    sys->GX3D.Status.FIFOHalfEmpty = true;
    sys->GX3D.Status.FIFOEmpty = true;
    sys->GX3D.BufferFree = true;

    sys->GX3D.GXPolyRAM = sys->GX3D.PolyRAMA;
    sys->GX3D.RenderPolyRAM = sys->GX3D.PolyRAMB;
    sys->GX3D.GXVtxRAM = sys->GX3D.VtxRAMA;
    sys->GX3D.RenderVtxRAM = sys->GX3D.VtxRAMB;
    sys->GX3D.TmpVertex.W = 1<<12;
    // checkme?
    sys->GX3D.PositionMatrix = IdentityMatrix;
    sys->GX3D.VectorMatrix = IdentityMatrix;
    sys->GX3D.ProjectionMatrix = IdentityMatrix;
    sys->GX3D.TextureMatrix = IdentityMatrix;

    RTC_Init(&sys->RTC);

    // TODO: are these always running?
    Schedule_Event(sys, AudioMixer_Sample, Evt_MixAudio, 0);
    Schedule_Event(sys, LCD_Scanline, Evt_Scanline, 0);

    // run power on/reset logic
    Console_Reset(sys);

    return sys;
}

void Console_DirectBoot(struct Console* sys)
{
    // wram should probably be enabled...?
    sys->WRAMCR = 3;
    sys->PostFlag = true;
    sys->Bios7Prot = 0x1204;
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

    //fseek(rom, 0x20, SEEK_SET);
    u32 arm9_romoffs = sys->Gamecard.ROM[0x20/4];
    u32 arm9_entryaddr = sys->Gamecard.ROM[0x24/4];
    u32 arm9_ramaddr = sys->Gamecard.ROM[0x28/4];
    u32 arm9_romsize = sys->Gamecard.ROM[0x2C/4];
    u32 arm7_romoffs = sys->Gamecard.ROM[0x30/4];
    u32 arm7_entryaddr = sys->Gamecard.ROM[0x34/4];
    u32 arm7_ramaddr = sys->Gamecard.ROM[0x38/4];
    u32 arm7_romsize = sys->Gamecard.ROM[0x3C/4];

    if (((arm9_romoffs + arm9_romsize) > sys->Gamecard.RomSize)
     || ((arm7_romoffs + arm7_romsize) > sys->Gamecard.RomSize))
    {
        LogPrint(LOG_ALWAYS, "ROM CONTAINS INVALID A9/A7 PROGRAMS\n");
        return;
    }

    //fread(vars, 4*4*2, 1, rom);

    sys->ARM9.ARM.R12 = arm9_entryaddr;
    sys->ARM9.ARM.R14 = arm9_entryaddr;
    sys->ARM7.ARM.R12 = arm7_entryaddr;
    sys->ARM7.ARM.R14 = arm7_entryaddr;

    timestamp nop;
    bool nopy;

    // load arm9 rom
    for (unsigned i = 0; i < arm9_romsize; i+=4)
    {
        AHB9_Write(sys, &nop, arm9_ramaddr+i, sys->Gamecard.ROM[(arm9_romoffs+i)/4], u32_max, false, &nopy, false);
    }

    // load arm7 rom
    for (unsigned i = 0; i < arm7_romsize; i+=4)
    {
        AHB7_Write(sys, &nop, arm7_ramaddr+i, sys->Gamecard.ROM[(arm7_romoffs+i)/4], u32_max, false, &nopy, false, 0);
    }

    sys->ARM9.CP15.CR.DTCMEnable = true;
    sys->ARM9.CP15.DTCMCR.Raw = 0x0300000A;
    ARM9_ConfigureDTCM(&sys->ARM9);

    // load header
    memcpy(&sys->MainRAM.b8[0x27FFE00 & (MainRAM_Size-1)], sys->Gamecard.ROM, 0x170);
    // "load" chipid
    sys->MainRAM.b32[(0x27FF800 & (MainRAM_Size-1))/sizeof(u32)] = 0x010101C2;
    sys->MainRAM.b32[(0x27FF804 & (MainRAM_Size-1))/sizeof(u32)] = 0x010101C2;
    sys->MainRAM.b32[(0x27FFC00 & (MainRAM_Size-1))/sizeof(u32)] = 0x010101C2;
    sys->MainRAM.b32[(0x27FFC04 & (MainRAM_Size-1))/sizeof(u32)] = 0x010101C2;

    // header checksum
    memcpy(&sys->MainRAM.b8[0x27FF808 & (MainRAM_Size-1)], &sys->Gamecard.ROM[0x15E/4]+2, 2);

    memcpy(&sys->MainRAM.b8[0x27FFC08 & (MainRAM_Size-1)], &sys->Gamecard.ROM[0x15E/4]+2, 2);

    // secure area checksum
    memcpy(&sys->MainRAM.b8[0x27FF80A & (MainRAM_Size-1)], &sys->Gamecard.ROM[0x6C/4], 2);

    memcpy(&sys->MainRAM.b8[0x27FFC0A & (MainRAM_Size-1)], &sys->Gamecard.ROM[0x6C/4], 2);

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

    for (int i = 0; i < 0x70; i++)
        sys->MainRAM.b8[((0x27FFC80 + i) & (MainRAM_Size-1))] = sys->Firmware.RAM[usersettings+i];

    ARM9_SetPC(&sys->ARM9, arm9_entryaddr, true, 0);
    ARM7_SetPC(&sys->ARM7, arm7_entryaddr, true);
    sys->DirectBoot = true;
}

void Console_Reset(struct Console* sys)
{
    ARM9_Reset(&sys->ARM9, false /*unverified I guess?*/, true);
    ARM7_Reset(&sys->ARM7);

    // TODO: reset dma?
}

timestamp Console_GetARM7Max(struct Console* sys, const bool froma9)
{
    timestamp ts;

    timestamp dmatime = DMA_GetNext(sys, false, froma9);
    ts = sys->ARM7.ARM.Timestamp;

    if (ts > dmatime)
        ts = dmatime;

    if (ts < sys->AHB7.Timestamp)
        ts = sys->AHB7.Timestamp;

    return ts;
}

timestamp Console_GetARM9Max(struct Console* sys, const bool froma7)
{
    timestamp ts;

    timestamp dmatime = DMA_GetNext(sys, true, froma7);
    ts = sys->ARM9.ARM.Timestamp >> ((sys->ARM9.BoostedClock) ? 2 : 1);

    if (ts > dmatime)
        ts = dmatime;

    if (ts < sys->AHB9.Timestamp)
        ts = sys->AHB9.Timestamp;

    return ts;
}

void Console_SyncWith7GTE(struct Console* sys, timestamp now, const bool bushogged)
{
    while(now >= Console_GetARM7Max(sys, true))
    {
        if (!bushogged && DMA_GetNext(sys, true, false) < Console_GetARM7Max(sys, true))
        {
            DMA_Run(sys, true);
        }
        CR_Switch(sys->HandleMain);
    }
}

void Console_SyncWith7GT(struct Console* sys, timestamp now, const bool bushogged)
{
    while(now > Console_GetARM7Max(sys, true))
    {
        if (!bushogged && DMA_GetNext(sys, true, false) <= (Console_GetARM7Max(sys, true) & (timestamp_max>>1)))
        {
            DMA_Run(sys, true);
        }
        CR_Switch(sys->HandleMain);
    }
}

void Console_SyncWith9GTE(struct Console* sys, timestamp now, const bool bushogged)
{
    while(now >= Console_GetARM9Max(sys, true))
    {
        if (!bushogged && DMA_GetNext(sys, false, false) < Console_GetARM9Max(sys, true))
        {
            DMA_Run(sys, false);
        }
        CR_Switch(sys->HandleMain);
    }
}

void Console_SyncWith9GT(struct Console* sys, timestamp now, const bool bushogged)
{
    while(now > Console_GetARM9Max(sys, true))
    {
        if (!bushogged && DMA_GetNext(sys, false, false) <= (Console_GetARM9Max(sys, true) & (timestamp_max>>1)))
        {
            DMA_Run(sys, false);
        }
        CR_Switch(sys->HandleMain);
    }
}

bool Console_CheckARM9Wake(struct Console* sys)
{
    return (sys->IME9 && (sys->IE9 & sys->IF9));
}

bool Console_CheckARM7Wake(struct Console* sys)
{
    return (sys->IE7 & sys->IF7);
}

void IF9_Update(struct Console* sys, timestamp now)
{
    timestamp time = sys->Sched.EventTimes[Evt_IF9Update];
    timestamp next = timestamp_max;
    for (int i = 0; i < IRQ_Max; i++)
    {
        if (time >= sys->IRQSched9[i])
        {
            sys->IF9 |= (1<<i);

            sys->IF9Held |= sys->IF9HoldQueue & (1<<i);
            sys->IF9HoldQueue &= ~(1<<i);

            sys->IRQSched9[i] = timestamp_max;
        }
        if (next > sys->IRQSched9[i])
            next = sys->IRQSched9[i];
    }

    // wake up cpu
    // TODO: SCHEDULE THIS INSTEAD
    if (sys->ARM9.ARM.CpuSleeping && Console_CheckARM9Wake(sys))
    {
        sys->ARM9.ARM.CpuSleeping = 0;
        sys->ARM9.ARM.Timestamp = now << ((sys->ARM9.BoostedClock) ? 2 : 1);
        ARM9_ExecuteCycles(&sys->ARM9, 1, 1);
        sys->ARM9.ARM.CodeSeq = false;
    }
    Schedule_Event(sys, IF9_Update, Evt_IF9Update, next);
}

void IF7_Update(struct Console* sys, timestamp now)
{
    timestamp time = sys->Sched.EventTimes[Evt_IF7Update];
    timestamp next = timestamp_max;
    for (int i = 0; i < IRQ_Max; i++)
    {
        if (time >= sys->IRQSched7[i])
        {
            sys->IF7 |= (1<<i);

            sys->IF7Held |= sys->IF7HoldQueue & (1<<i);
            sys->IF7HoldQueue &= ~(1<<i);

            sys->IRQSched7[i] = timestamp_max;
        }
        if (next > sys->IRQSched7[i])
            next = sys->IRQSched7[i];
    }
    // wake up cpu
    // TODO: SCHEDULE THIS INSTEAD
    if (sys->ARM7.ARM.CpuSleeping && Console_CheckARM7Wake(sys))
    {
        sys->ARM7.ARM.CpuSleeping = 0;
        sys->ARM7.ARM.Timestamp = now;
        ARM7_ExecuteCycles(&sys->ARM7, 1);
        sys->ARM7.ARM.CodeSeq = false;
    }
    Schedule_Event(sys, IF7_Update, Evt_IF7Update, next);
}

void Console_ScheduleIRQs(struct Console* sys, const u8 irq, const bool a9, timestamp time)
{
    //if (irq == IRQ_VBlank) printf("irq: %lu\n", time);
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
        Schedule_Event(sys, IF9_Update, Evt_IF9Update, time);
    else
        Schedule_Event(sys, IF7_Update, Evt_IF7Update, time);
}

void Console_ScheduleHeldIRQs(struct Console* sys, const u8 irq, const bool a9, timestamp time)
{
    if (a9) sys->IF9HoldQueue |= 1<<irq;
    else    sys->IF7HoldQueue |= 1<<irq;

    Console_ScheduleIRQs(sys, irq, a9, time);
}

void Console_ClearHeldIRQs(struct Console* sys, const u8 irq, const bool a9)
{
    if (a9)
    {
        sys->IF9HoldQueue &= ~(1<<irq);
        sys->IF9Held &= ~(1<<irq); // idk??
    }
    else
    {
        sys->IF7HoldQueue &= ~(1<<irq);
        sys->IF7Held &= ~(1<<irq); // idk??
    }
    Console_ScheduleIRQs(sys, irq, a9, timestamp_max);
}

void Console_MainLoop(struct Console* sys)
{
    CR_Start = true;
    mtx_lock(&sys->FrameBufferMutex[sys->BackBuf]);
    Scheduler_UpdateTargets(sys);
    sys->TimeFrac = 0;
    sys->OldTime = SDL_GetPerformanceCounter();
    sys->log = fopen("audioout.bin", "wb");
    while(!sys->KillThread)
    {
#ifdef UseThreads
        while ((Console_GetARM9Max(sys, true) < sys->ARM7Target) || (Console_GetARM7Max(sys, true) < sys->ARM7Target)); //printf("9 %li %li 7 %li %li\n", sys->ARM9.ARM.Timestamp, sys->ARM9Target, sys->ARM7.ARM.Timestamp, sys->ARM7Target);
#else
        bool exit = false;
        while(!exit)//(Console_GetARM7Max(sys, true) < sys->ARM7Target) || (Console_GetARM9Max(sys, true) < sys->ARM7Target))
        {
            //printf("9i %lu %lu s:%i\n", Console_GetARM9Max(sys, true), sys->ARM7Target, sys->ARM9.ARM.DeadAsleep);
            //printf("7i %lu %lu s:%i\n", Console_GetARM7Max(sys, true), sys->ARM7Target, sys->ARM7.ARM.DeadAsleep);
            //printf("%lX %lX %lX %lX\n", sys->ARM9.ARM.Timestamp, sys->ARM9.MemTimestamp, sys->AHB9.Timestamp, sys->DMA9.NextTime);
            //if (sys->ARM9.ARM.Timestamp > 0x3FFFFFFFFFFFFFFF || sys->ARM9.MemTimestamp > 0x3FFFFFFFFFFFFFFF || sys->AHB9.Timestamp > 0x3FFFFFFFFFFFFFFF || sys->ARM7.ARM.Timestamp > 0x3FFFFFFFFFFFFFFF || sys->AHB7.Timestamp > 0x3FFFFFFFFFFFFFFF) CrashSpectacularly("TIMESTAMP BORK\n");
            exit = true;

            if (Console_GetARM9Max(sys, true) < sys->ARM7Target)
            {
                CR_Switch(sys->HandleARM9);
                exit = false;
            }
            if (Console_GetARM7Max(sys, true) < sys->ARM7Target)
            {
                CR_Switch(sys->HandleARM7);
                exit = false;
            }
            //if ((Console_GetARM7Max(sys) > sys->ARM7Target) && (Console_GetARM9Max(sys, true) > sys->ARM7Target)) printf("zzz, 9: %lu %08X %08X 7: %lu %08X %08X\n", Console_GetARM9Max(sys), sys->IE9, sys->IF9, Console_GetARM7Max(sys), sys->IE7, sys->IF7);
        }
        //printf("9e %lu %lu s:%i\n", Console_GetARM9Max(sys, true), sys->ARM7Target, sys->ARM9.ARM.DeadAsleep);
        //printf("%08X %08X\n", sys->ARM9.ARM.LR, sys->ARM9.ARM.PC);
        //printf("7e %lu %lu s:%i\n", Console_GetARM7Max(sys, true), sys->ARM7Target, sys->ARM7.ARM.DeadAsleep);
#endif
        Scheduler_Run(sys);
    }
    return;
}
