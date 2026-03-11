#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL.h>
#include <threads.h>
#include "video.h"
#include "../scheduler.h"
#include "../console.h"
#include "../io/dma.h"
#include "../utils.h"
#include "ppu.h"
#include "3d.h"




void PPU_SetTarget(struct Console* sys, const timestamp now)
{
#ifndef PPUST
    if (sys->PPUTarget < now)
        sys->PPUTarget = now;
#else
    PPU_RenderScanline(sys, false, sys->VCount);
    PPU_RenderScanline(sys, true, sys->VCount);
#endif
}

void PPU_Sync(struct Console* sys, timestamp now)
{
#ifndef PPUST
    if (!sys->PPUStart) return;
    PPU_SetTarget(sys, now);
    while ((sys->PPUATimestamp < now) || (sys->PPUBTimestamp < now)) thrd_yield();
#endif
}

void PPU_Wait(struct Console* sys, const timestamp now)
{
#ifndef PPUST
    while (now >= sys->PPUTarget) thrd_yield();
#endif
}

void PPU_Init(struct Console* sys, const timestamp now)
{
#ifndef PPUST
    if (sys->PPUStart) return;
    sys->PPUTarget = now;
    sys->PPUATimestamp = now;
    sys->PPUBTimestamp = now;
    sys->PPUStart = true;
#endif
}

void LCD_HBlank(struct Console* sys, timestamp now)
{
    // set hblank flag
    sys->DispStatRO9.HBlank = true;
    sys->DispStatRO7.HBlank = true;
    if (sys->VCount == 262) 
        PPU_SetTarget(sys, now);
    if (sys->VCount < 192)
    {
        StartDMA9(sys, now+2+1, DMAStart_HBlank); // checkme: delay?
        PPU_SetTarget(sys, now);
    }
    if (sys->VCount == 192)
    {
        PPU_Sync(sys, now);
        sys->RenderedLines = 0;
        sys->BackBuf = !sys->BackBuf;
        mtx_lock(&sys->FrameBufferMutex[sys->BackBuf]);
        mtx_unlock(&sys->FrameBufferMutex[!sys->BackBuf]);

        // frame limiter
        {
            // TODO: this doesn't really work quite right if you take longer than a frame.
            u64 target = sys->OldTime + ((sys->CountPerFrame + sys->TimeFrac) / Base_Clock);
            sys->TimeFrac = (sys->CountPerFrame + sys->TimeFrac) % Base_Clock;
            double frametimeactual = (double)(SDL_GetPerformanceCounter() - sys->OldTime) * 1000.0 / SDL_GetPerformanceFrequency();
            while(SDL_GetPerformanceCounter() < target) thrd_yield();
            double frametime = (double)(SDL_GetPerformanceCounter() - sys->OldTime) * 1000.0 / SDL_GetPerformanceFrequency();
            sys->OldTime = target;
            sys->FrameTime = frametime;
            sys->FrameTimeActual = frametimeactual;

#ifdef MonitorFPS
            LogPrint(LOG_ALWAYS, "%lu\n", sys->FrameTime);
#endif
        }
    }
    // schedule irq
    if (sys->DispStatRW9.HBlankIRQ) Console_ScheduleIRQs(sys, IRQ_HBlank, true, now+2); // CHECKME: delay?
    if (sys->DispStatRW7.HBlankIRQ) Console_ScheduleIRQs(sys, IRQ_HBlank, false, now+2); // CHECKME: delay?

    // schedule hblank
    Schedule_Event(sys, LCD_Scanline, Evt_Scanline, now + HBlank_Cycles);
}

void LCD_Scanline(struct Console* sys, timestamp now)
{
    sys->VCount++;
    sys->VCount &= 0x1FF;
    if (sys->VCount == 263) sys->VCount = 0;
    //ARM9_Log(&sys->ARM9);
    //ARM7_Log(&sys->ARM7);

    // check for vblank; clear hblank.
    // this occurs before vcount writes
    if (sys->VCount == 192)
    {
        if (SDL_GetGamepadButton(sys->Pad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) // debugging junk
        {

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

        sys->DispStatRO9.Raw = 0b001;
        sys->DispStatRO7.Raw = 0b001;

        // schedule irq
        if (sys->DispStatRW9.VBlankIRQ) Console_ScheduleIRQs(sys, IRQ_VBlank, true, now+2);
        if (sys->DispStatRW7.VBlankIRQ) Console_ScheduleIRQs(sys, IRQ_VBlank, false, now+2); // CHECKME: delay correct for arm7 too?
        StartDMA9(sys, now+2+1, DMAStart_VBlank); // checkme: delay?
        StartDMA9(sys, now+2+1, DMAStart_VBlank); // checkme: delay?

        SWRen_Sync(sys, now);
        GX_Swap(sys, now);
    }
    else if (sys->VCount == 262)
    {
        sys->DispStatRO9.Raw = 0b000;
        sys->DispStatRO7.Raw = 0b000;
        PPU_Init(sys, now);
    }
    else
    {
        // just clear hblank
        sys->DispStatRO9.HBlank = false;
        sys->DispStatRO7.HBlank = false;
    }

    if (sys->VCount == 214)
    {
        SWRen_Init(sys, now);
        SWRen_SetTarget(sys, now+Scanline_Cycles*263);
        //SWRen_RasterizerFrame(sys);
    }

    // i dont 100% trust my testing here but it seems like if both cpus write to vcount on the same scanline the arm9 wins out?
    if (sys->VCountUpdate9)
    {
        //sys->VCount = sys->VCountNew9; TODO: REFACTOR PPU/LCD LOGIC TO ADD SUPPORT FOR THIS
        sys->VCountUpdate9 = false;
        sys->VCountUpdate7 = false;
    }
    if (sys->VCountUpdate7)
    {
        //sys->VCount = sys->VCountNew7; TODO: REFACTOR PPU/LCD LOGIC TO ADD SUPPORT FOR THIS
        sys->VCountUpdate9 = false;
        sys->VCountUpdate7 = false;
    }

    // vcount match
    sys->DispStatRO7.VCountMatch = (sys->TargetVCount7 == sys->VCount);
    if (sys->TargetVCount7 == sys->VCount) Console_ScheduleIRQs(sys, IRQ_VCount, false, now+2); // checkme: delay?
    sys->DispStatRO9.VCountMatch = (sys->TargetVCount9 == sys->VCount);
    if (sys->TargetVCount9 == sys->VCount) Console_ScheduleIRQs(sys, IRQ_VCount, true, now+2); // checkme: delay?

    // schedule hblank
    Schedule_Event(sys, LCD_HBlank, Evt_Scanline, now + ActiveRender_Cycles);
}
