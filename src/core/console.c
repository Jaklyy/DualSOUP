#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_gamepad.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
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


void Console_DebugDump(Console* sys)
{
    printf("Dumping\n");

    FILE* file = fopen("logfcram.bin", "wb");
    fwrite(sys->MainRAM.b8, sizeof(sys->MainRAM), 1, file);
    fclose(file);

    file = fopen("logdtcm.bin", "wb");
    fwrite(sys->A946ES.DTCM.b8, sizeof(sys->A946ES.DTCM.b8), 1, file);
    fclose(file);

    file = fopen("logitcm.bin", "wb");
    fwrite(sys->A946ES.ITCM.b8, sizeof(sys->A946ES.ITCM.b8), 1, file);
    fclose(file);

    file = fopen("logswram.bin", "wb");
    fwrite(sys->SharedWRAM.b8, sizeof(sys->SharedWRAM.b8), 1, file);
    fclose(file);

    file = fopen("log7wram.bin", "wb");
    fwrite(sys->ARM7WRAM.b8, sizeof(sys->ARM7WRAM.b8), 1, file);
    fclose(file);

    printf("Done.\n");
}

void Console_DebugLog(Console* sys)
{
#if 0
    printf("%i %i\n", sys->A946ES.BIU.WBuffer.Empty, sys->A946ES.BIU.WBuffer.Full);
#elif 1
    A946_Log(&sys->A946ES);
    A946_DumpMPU(&sys->A946ES);
    A7TDMI_Log(&sys->A7TDMI);
    Sched_Log(sys);
    printf("fcram: %i %i\n", sys->BusMR.CurReq, sys->BusMR.Locked);
    Console_DebugDump(sys);
//#elif 0
#elif 0
    for (int i = 0; i < 16; i++)
    {
        printf("channel %i: dmacr:%08X dmats:%08lX dmasa:%08X dmaln%08X dmamd%i\nchraw: %08X cen:%i cfm:%i crm:%i chln%i chlp%i chpr:%08X chmx:%08lX\ntimercr:%06X fifd:%i fiff:%i fifs:%i\n", i, \
        sys->DMA7.Channels[i].CR.Raw, sys->DMA7.ChannelTimestamps[i], sys->DMA7.Channels[i].Latched_SrcAddr, sys->DMA7.Channels[i].Latched_NumWords, sys->DMA7.Channels[i].CurrentMode, \
        sys->SoundChannels[i].CR.Raw, sys->SoundChannels[i].CR.Enable, sys->SoundChannels[i].CR.Format, sys->SoundChannels[i].CR.RepeatMode, sys->SoundChannels[i].SoundLen, sys->SoundChannels[i].LoopOffs, \
        sys->SoundChannels[i].Prog, sys->SoundChannels[i].SampleMax, \
        sys->Timers7[i+4].Regs, sys->SoundChannels[i].FIFO_DrainPtr, sys->SoundChannels[i].FIFO_FillPtr, sys->SoundChannels[i].FIFO_Bytes);
    }
    printf("dma cur: %08X\n", sys->DMA7.CurMask);
#elif 0
    bool seq = false;
    printf("dumping\n");
    {
        FILE* file = fopen("log7.bin", "wb");
        for (int i = 0x02000000; i < 0x08000000; i+=4)
        {
            u32 buf = AHB7_Read(sys, NULL, i, HSIZE_32, false, false, &seq, false, 0);
            fwrite(&buf, 4, 1, file);
        }
        fclose(file);
    }
    {
        FILE* file = fopen("log9dt.bin", "wb");
        fwrite(sys->ARM9.DTCM.b8, A946_DTCMSize, 1, file);
        fclose(file);
    }
    {
        FILE* file = fopen("log9it.bin", "wb");
        fwrite(sys->ARM9.ITCM.b8, A946_ITCMSize, 1, file);
        fclose(file);
    }
    {
        FILE* file = fopen("log9.bin", "wb");
        for (int i = 0x02000000; i < 0x08000000; i+=4)
        {
            u32 buf = AHB9_Read(sys, NULL, i, HSIZE_32, false, false, &seq, false);
            fwrite(&buf, 4, 1, file);
        }
        fclose(file);
    }
    printf("done\n");
    while (SDL_GetGamepadButton(sys->Pad, SDL_GAMEPAD_BUTTON_LEFT_STICK));
#endif
}

bool Console_ReadFile(u8* buf, const char* path, const size_t num, const char* name)
{
    SDL_IOStream* file;
    if ((file = SDL_IOFromFile(path, "rb")))
    {
        if (SDL_ReadIO(file, buf, num) == num)
        {
            SDL_CloseIO(file);
            return true;
        }
        else
        {
            SDL_CloseIO(file);
            printf("%s File Read Error: idk ask jakly to actually add the proper diagnostics, but ig it couldn't read the full file\n", name);
        }
    }
    else
    {
        printf("%s File Open Error: %s\n", name, SDL_GetError());
    }
    return false;
}


void Console_Cleanup(Console* sys, bool full)
{
    if (sys == nullptr) return;

    // these dont need to be re-init actually?
    int dummy;
#ifndef SINGLETHREADRASTER
    // todo: detach threads instead?
    // todo: obliterate threads without asking politely instead?
    sys->KillSWRen = true;
    sys->SWRenStart = true;
    sys->SWRenTarget = timestamp_max;
    SDL_WaitThread(sys->SWRenThread, &dummy); 
    sys->RenderedLines = 255;
    sys->KillPPUs = true;
    sys->PPUStart = true;
    sys->PPUTarget = timestamp_max;
    SDL_WaitThread(sys->PPUAThread, &dummy);
    SDL_WaitThread(sys->PPUBThread, &dummy);
#endif
    // i tried reusing these and it hung, so i gave up, because it probably doesn't matter
    SDL_UnlockMutex(sys->FrameBufferMutex[sys->BackBuf]); // freeing a locked mutex is apparently undefined behavior, neat
    SDL_DestroyMutex(sys->FrameBufferMutex[0]);
    SDL_DestroyMutex(sys->FrameBufferMutex[1]);

    // flash and gamecards need to be recreated from scratch in case we're booting a new game/system
    Flash_Cleanup(&sys->Firmware);
    GameCard_Cleanup(&sys->GameCard);

    if (full) SDL_aligned_free(sys);
}

// TODO: this function probably shouldn't manage memory on its own?
// TODO: this function is a complete mess. it NEEDS to be restructured heavily at some point.
Console* Console_Init(Console* sys, CoreCfg* cfg, void* aud)
{
    int cleanup = 0;
    if (sys == nullptr)
    {
        // fresh init, allocate everything

        // use SDL function for this because windows SUCKS
        if ((sys = SDL_aligned_alloc(alignof(Console), sizeof(Console))) == NULL)
        {
            LogPrint(LOG_ALWAYS, "FATAL: Memory allocation failed.\n");
            goto fail;
        }
        cleanup++;
    }
    else
    {
        Console_Cleanup(sys, false);
        // reusing an allocated struct, cleanup internal variables
        cleanup++;
    }

    // wipe entire emulator state
    memset(sys, 0, sizeof(*sys));

    if ((sys->FrameBufferMutex[0] = SDL_CreateMutex()) == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Mutex init failed. %s\n", SDL_GetError());
        goto fail;
    }
    cleanup++;
    if ((sys->FrameBufferMutex[1] = SDL_CreateMutex()) == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Mutex init failed. %s\n", SDL_GetError());
        goto fail;
    }
    cleanup++;

#ifndef SINGLETHREADRASTER
    if ((sys->PPUAThread = SDL_CreateThread(PPUA_MainLoop, "SOUP_PPUA", sys)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: PPUA Thread creation failed. %s\n", SDL_GetError());
        goto fail;
    }
    cleanup++;
    if ((sys->PPUAThread = SDL_CreateThread(PPUB_MainLoop, "SOUP_PPUB", sys)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: PPUB Thread creation failed. %s\n", SDL_GetError());
        goto fail;
    }
    cleanup++;
    if ((sys->SWRenThread = SDL_CreateThread(SWRen_MainLoop, "SOUP_GPUR", sys)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: 3D Rasterizer Thread creation failed. %s\n", SDL_GetError());
        goto fail;
    }
    cleanup++;
#else
    cleanup+=3;
#endif

    sys->SysCfg = cfg->SysCfg;
    sys->Aud = aud;

    if (!Console_ReadFile(sys->NTRBios9.b8, cfg->NTR.Bios9, NTRBios9_Size, "DS ARM9 Bios")) goto fail;
    if (!Console_ReadFile(sys->NTRBios7.b8, cfg->NTR.Bios7, NTRBios7_Size, "DS ARM7 Bios")) goto fail;

    size_t nvramsize;
    switch(sys->SysCfg.WiFiNVRAMSize)
    {
        //case WiFiNVRAM_4KiB: nvramsize = KiB(4); break; TODO: dumpers typically overdump these as 128KiB(?) The address space is also still 128 KiB, its just most of it is unmapped iirc.
        case WiFiNVRAM_128KiB: nvramsize = KiB(128); break;
        default: LogPrint(LOG_ALWAYS, "UNHANDLED ENUM FOR WIFI NVRAM SIZE DURING CORE INIT!!!\n"); [[fallthrough]];
        case WiFiNVRAM_256KiB: nvramsize = KiB(256); break;
        case WiFiNVRAM_512KiB: nvramsize = KiB(512); break;
    }
    u8* nvram;
    if ((nvram = malloc(nvramsize)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Failed to allocate memory for firmware nvram\n");
        goto fail;
    }
    cleanup++;
    if (!Console_ReadFile(nvram, cfg->NTR.NVRAM, nvramsize, "DS Firmware")) goto fail;
    cleanup++;
    Flash_Init(&sys->Firmware, nvram, nvramsize, sys->SysCfg.WiFiNVRAMWriteProt, 0x010101);

    if (!GameCard_Init(&sys->GameCard, &cfg->SysCfg.GameCard, sys->NTRBios7.b8)) goto fail;
    cleanup++;

    GamePak_Init(&sys->GamePak);

    SDL_LockMutex(sys->FrameBufferMutex[sys->BackBuf]);

    // init variables

    A946_Init(&sys->A946ES, sys);
    A7TDMI_Init(&sys->A7TDMI, sys);

    for (s32 i = 0; i < Evt_Max; i++)
    {
        sys->Sched.Times[i] = timestamp_max;
        sys->Sched.Next[i] = Evt_Invalid;
        sys->Sched.Prev[i] = Evt_Invalid;
    }

    for (s32 i = 0; i < IRQ_Max; i++)
        sys->IRQSched9[i] = timestamp_max;

    for (s32 i = 0; i < IRQ_Max; i++)
        sys->IRQSched7[i] = timestamp_max;

    DMA_Init(sys);

    IPC_FIFOInit(&sys->IPCFIFO7);
    IPC_FIFOInit(&sys->IPCFIFO9);

    PMIC_Init(sys);

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

    Bus9_Init(&sys->Bus9);
    Bus7_Init(&sys->Bus7);
    MainRAM_Init(sys, cfg->SysCfg.NTRFCRAM);

    RTC_Init(&sys->RTC);

    WiFi_Init(sys);

    // TODO: are these always running?
    Sched_AddEvent(sys, 0, Evt_MixAudio);
    Sched_AddEvent(sys, 0, Evt_Scanline);

#ifdef DUMPAUDIO
    sys->log = fopen("audioout.bin", "wb");
#endif

    // run power on/reset logic
    Console_Reset(sys);

    return sys;

    fail:
    int dummy;
    switch (cleanup)
    {
        default: LogPrint(LOG_ALWAYS, "UNHANDLED CLEANUP SWITCH\n"); [[fallthrough]];
        case 8: GameCard_Cleanup(&sys->GameCard); [[fallthrough]];
        case 7: Flash_Cleanup(&sys->Firmware); [[fallthrough]];
#ifndef SINGLETHREADRASTER
        case 6:
        sys->KillSWRen = true;
        sys->SWRenStart = true;
        sys->SWRenTarget = timestamp_max;
        SDL_WaitThread(sys->SWRenThread, &dummy); // todo: detach thread instead?
        [[fallthrough]];
        case 5:
        sys->RenderedLines = 255;
        sys->KillPPUs = true;
        sys->PPUStart = true;
        sys->PPUTarget = timestamp_max;
        SDL_WaitThread(sys->PPUAThread, &dummy); // todo: detach thread instead?
        [[fallthrough]];
        case 4:
        sys->RenderedLines = 255;
        sys->KillPPUs = true;
        sys->PPUStart = true;
        sys->PPUTarget = timestamp_max;
        SDL_WaitThread(sys->PPUBThread, &dummy); // todo: detach thread instead?
        [[fallthrough]];
#else
        case 4 ... 6: [[fallthrough]]; // unimplemented
#endif
        case 3: SDL_DestroyMutex(sys->FrameBufferMutex[1]); [[fallthrough]];
        case 2: SDL_DestroyMutex(sys->FrameBufferMutex[0]); [[fallthrough]];
        case 1: SDL_aligned_free(sys); [[fallthrough]];
        case 0: return nullptr;
    }
}

void Console_DirectBoot(Console* sys)
{
    u32 arm9_romoffs = sys->GameCard.ROM[0x20/4];
    u32 arm9_entryaddr = sys->GameCard.ROM[0x24/4];
    u32 arm9_ramaddr = sys->GameCard.ROM[0x28/4];
    u32 arm9_romsize = sys->GameCard.ROM[0x2C/4];

    u32 arm7_romoffs = sys->GameCard.ROM[0x30/4];
    u32 arm7_entryaddr = sys->GameCard.ROM[0x34/4];
    u32 arm7_ramaddr = sys->GameCard.ROM[0x38/4];
    u32 arm7_romsize = sys->GameCard.ROM[0x3C/4];

    if (((arm9_romoffs + arm9_romsize) > sys->GameCard.RomSize)
     || ((arm7_romoffs + arm7_romsize) > sys->GameCard.RomSize))
    {
        LogPrint(LOG_ALWAYS, "ROM CONTAINS INVALID A9/A7 PROGRAMS\n");
        return;
    }

    // wram should probably be enabled...?
    sys->WRAMCR = 3;
    sys->PostFlag = true;
    sys->Bios7Prot = 0x1204;
    sys->PowerCR9.Raw = 0x820F;

    sys->GameCard.Mode = Key2;

    // set main ram bits to be enabled
    sys->ExtMemCR_Shared.MRSomething1 = true;
    sys->ExtMemCR_Shared.MRSomething2 = true;
    sys->ExtMemCR_Shared.MRA7Priority = true; // ARM7
    sys->ExtMemCR_Shared.GBAPakA7Access = true;
    sys->ExtMemCR_Shared.NDSCardA7Access = true;

    ARM_SetMode((ARM*)&sys->A946ES, ARMMode_SYS);
    ARM_SetMode((ARM*)&sys->A7TDMI, ARMMode_SYS);
    sys->A946ES.ARM.SP = 0x03002F7C;
    sys->A946ES.ARM.IRQ_Bank.R[0] = 0x03003F80;
    sys->A946ES.ARM.SVC_Bank.R[0] = 0x03003FC0;
    sys->A7TDMI.ARM.SP = 0x0380FD80;
    sys->A7TDMI.ARM.IRQ_Bank.R[0] = 0x0380FF80;
    sys->A7TDMI.ARM.SVC_Bank.R[0] = 0x0380FFC0;

    sys->A946ES.ARM.R12 = arm9_entryaddr;
    sys->A946ES.ARM.R14 = arm9_entryaddr;
    sys->A7TDMI.ARM.R12 = arm7_entryaddr;
    sys->A7TDMI.ARM.R14 = arm7_entryaddr;

    const u32 mrmask = (MainRAM_Size-1) & (sys->BusMR.AddrSubmMask);
    // load arm9 rom
    for (u32 i = 0; i < arm9_romsize; i+=4)
    {
        u32 addr = arm9_ramaddr+i;
        u32 romaddr = arm9_romoffs+i;
        if ((addr & 0xFF000000) == 0x02000000)
        {
            sys->MainRAM.b32[(addr & mrmask)/4] = sys->GameCard.ROM[(romaddr & (sys->GameCard.RomSize-1))/4];
        }
    }

    // load arm7 rom
    for (u32 i = 0; i < arm7_romsize; i+=4)
    {
        u32 addr = arm7_ramaddr+i;
        u32 romaddr = arm7_romoffs+i;
        if ((addr & 0xFF000000) == 0x02000000)
        {
            sys->MainRAM.b32[(addr & mrmask)/4] = sys->GameCard.ROM[(romaddr & (sys->GameCard.RomSize-1))/4];
        }
        else if ((addr & 0xFF800000) == 0x03000000) // assume its always fully mapped to arm7
        {
            sys->SharedWRAM.b32[(addr & (SharedWRAM_Size-1))/4] = sys->GameCard.ROM[(romaddr & (sys->GameCard.RomSize-1))/4];
        }
        else if ((addr & 0xFF800000) == 0x03800000)
        {
            sys->ARM7WRAM.b32[(addr & (ARM7WRAM_Size-1))/4] = sys->GameCard.ROM[(romaddr & (sys->GameCard.RomSize-1))/4];
        }
    }

    sys->A946ES.CP15.CR.DTCMEnable = true;
    sys->A946ES.CP15.DTCMCR.Raw = 0x0300000A;
    A946_ConfigureDTCM(&sys->A946ES);

    // load header
    memcpy(&sys->MainRAM.b8[0x27FFE00 & mrmask], sys->GameCard.ROM, 0x170);
    // "load" chipid
    sys->MainRAM.b32[(0x27FF800 & mrmask)/4] = 0x010101C2;
    sys->MainRAM.b32[(0x27FF804 & mrmask)/4] = 0x010101C2;
    sys->MainRAM.b32[(0x27FFC00 & mrmask)/4] = 0x010101C2;
    sys->MainRAM.b32[(0x27FFC04 & mrmask)/4] = 0x010101C2;

    // header checksum
    memcpy(&sys->MainRAM.b8[0x27FF808 & mrmask], (void*)(((intptr_t)&(sys->GameCard.ROM[0x15E/4]))+2), 2);

    memcpy(&sys->MainRAM.b8[0x27FFC08 & mrmask], (void*)(((intptr_t)&sys->GameCard.ROM[0x15E/4])+2), 2);

    // secure area checksum
    memcpy(&sys->MainRAM.b8[0x27FF80A & mrmask], &sys->GameCard.ROM[0x6C/4], 2);

    memcpy(&sys->MainRAM.b8[0x27FFC0A & mrmask], &sys->GameCard.ROM[0x6C/4], 2);

    // idk
    sys->MainRAM.b16[(0x27FF850 & mrmask)/2] = 0x5835;
    sys->MainRAM.b16[(0x27FFC10 & mrmask)/2] = 0x5835;
    sys->MainRAM.b16[(0x27FFC30 & mrmask)/2] = 0xFFFF;
    sys->MainRAM.b16[(0x27FFC40 & mrmask)/2] = 0x0001;

    u16 usersettings = (sys->Firmware.RAM[0x20] | (sys->Firmware.RAM[0x21] << 8))*8;

    sys->MainRAM.b32[((0x27FF864) & mrmask)/4] = 0;
    sys->MainRAM.b32[((0x27FF868) & mrmask)/4] = usersettings;

    sys->MainRAM.b16[((0x27FF874) & mrmask)/2] = (sys->Firmware.RAM[0x26] | (sys->Firmware.RAM[0x27] << 8));

    sys->MainRAM.b16[((0x27FF876) & mrmask)/2] = (sys->Firmware.RAM[4] | (sys->Firmware.RAM[5] << 8));

    for (int i = 0; i < 0x70; i++)
        sys->MainRAM.b8[((0x27FFC80 + i) & mrmask)] = sys->Firmware.RAM[usersettings+i];

    A9ES_SetPC(&sys->A946ES, arm9_entryaddr);
    A7TDMI_SetPC(&sys->A7TDMI, arm7_entryaddr);
    // hack
    memset(&sys->Bus7, 0, sizeof(sys->Bus7));
    Bus7_Init(&sys->Bus7);
    A7TDMI_InstrRead(&sys->A7TDMI, 0);
    sys->DirectBoot = true;
}

void Console_Reset(Console* sys)
{
    A946_Reset(&sys->A946ES, false /*unverified I guess?*/, true);
    A7TDMI_Reset(&sys->A7TDMI);

    // todo: move into actual reset handlers
    A9ES_InstrGo(&sys->A946ES, false);
    Sched_AddEvent(sys, 0, Evt_ARM9);

    A7TDMI_InstrRead(&sys->A7TDMI, 0);
    // TODO: reset dma?
}

void IRQ9_Update(Console* sys, const timestamp now)
{
    // send irq to arm9
    sys->A946ES.ARM.InterruptRequest = (sys->IME9 && (sys->IF9 & sys->IE9));

    // arm946e-s wakes when interrupt is raised, regardless of cpsr bit
    if (sys->A946ES.ARM.InterruptRequest && sys->A946ES.ARM.WaitForInterrupt)
    {
        sys->A946ES.ARM.WaitForInterrupt = false;
        Sched_AddEvent(sys, now, Evt_ARM9);
    }
}

void IF9_Clear(Console* sys, u32 wrdata, const timestamp now)
{
    sys->IF9 &= ~wrdata | sys->IF9Persist;
    Sched_AddEvent(sys, now+DSClk33(1), Evt_UpdateIRQ9);
}

void IF9_Set(Console* sys, const IRQIDs id, const timestamp now)
{
    sys->IF9 |= (u32)1<<id;
    sys->IF9Persist |= ((u32)1<<id) & IRQ9_LevelSens;
    Sched_AddEvent(sys, now+DSClk33(1), Evt_UpdateIRQ9);
}

void LevelIRQ9_Stop(Console* sys, const IRQIDs id)
{
    sys->IF9Persist &= ~((u32)1<<id);
}

// arm7 halt is implemented by the SoC hardware
// it checks for IE and IF; IME only prevents irqs from signaling the processor
bool Console_CheckARM7Wake(Console* sys)
{
    return (sys->IE7 & sys->IF7);
}

void IRQ7_Update(Console* sys, const timestamp now)
{
    // send irq to arm7
    sys->A7TDMI.ARM.InterruptRequest = (sys->IME7 && (sys->IF7 & sys->IE7));

    // gba/nds uses an external mechanism for power saving for the arm7tdmi
    if (sys->A7ClkDisable && Console_CheckARM7Wake(sys))
        Bus7_A7Wake(sys, now);
}

void IF7_Clear(Console* sys, u32 wrdata, const timestamp now)
{
    sys->IF7 &= ~wrdata | sys->IF7Persist;
    Sched_AddEvent(sys, now+DSClk33(1), Evt_UpdateIRQ7);
}

void IF7_Set(Console* sys, const IRQIDs id, const timestamp now)
{
    sys->IF7 |= (u64)1<<id;
    sys->IF7Persist |= ((u64)1<<id) & IRQ7_LevelSens;
    Sched_AddEvent(sys, now+DSClk33(1), Evt_UpdateIRQ7);
}

bool Console_TestIfPollingNeeded(Console* sys, timestamp now)
{
    return (now - sys->LastPoll) > (Frame_Cycles * (Sched_Clock / NTR_SysClock) / 4);
}

Core_Ret Console_MainLoop(Console* sys)
{
    Core_Ret ret;
    do
    {
        ret = Sched_RunEvent(sys);
    } while(ret == Core_Continue);
    return ret;
}
