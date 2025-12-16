#include <SDL3/SDL_timer.h>
#include <SDL3/SDL.h>
#include <threads.h>
#include "video.h"
#include "../bus/io.h"
#include "../scheduler.h"
#include "../console.h"
#include "../io/dma.h"
#include "../utils.h"
#include "ppu.h"
#include "3d.h"




void PPU_SetTarget(struct Console* sys, const timestamp now)
{
    sys->PPUTarget = now>>1;
}

void PPU_Sync(struct Console* sys, timestamp now)
{
    if (!sys->PPUStart) return;
    PPU_SetTarget(sys, now);
    now >>= 1;
    //printf("%li %li %li\n", sys->PPUATimestamp, sys->PPUBTimestamp, now);
    while ((sys->PPUATimestamp < now) || (sys->PPUBTimestamp < now)) thrd_yield();
}

void PPU_Wait(struct Console* sys, const timestamp now)
{
    while (now >= sys->PPUTarget) thrd_yield();
}

void PPU_Init(struct Console* sys, const timestamp now)
{
    if (sys->PPUStart) return;
    sys->PPUTarget = now;
    sys->PPUATimestamp = now;
    sys->PPUBTimestamp = now;
    sys->PPUStart = true;
}

void LCD_HBlank(struct Console* sys, timestamp now)
{
    // set hblank flag
    sys->DispStatRO9.HBlank = true;
    sys->DispStatRO7.HBlank = true;
    if (sys->VCount == 0) PPU_Init(sys, now);
    if (sys->VCount < 192)
    {
        StartDMA9(sys, now+2+1, DMAStart_HBlank); // checkme: delay?
        //PPU_RenderScanline(sys, false, sys->VCount);
        //PPU_RenderScanline(sys, true, sys->VCount);
        //printf("%i\n", sys->VCount);
        PPU_SetTarget(sys, now);
    }
    if (sys->VCount == 192)
    {
        PPU_Sync(sys, now);
        sys->BackBuf = !sys->BackBuf;
        mtx_lock(&sys->FrameBufferMutex[sys->BackBuf]);
        mtx_unlock(&sys->FrameBufferMutex[!sys->BackBuf]);

        //while ((SDL_GetPerformanceCounter() - sys->OldTime) < sys->CyclesPerFrame);
        //sys->OldTime = SDL_GetPerformanceCounter();
    }
    // schedule irq
    if (sys->DispStatRW9.HBlankIRQ) Console_ScheduleIRQs(sys, IRQ_HBlank, true, now+2); // CHECKME: delay?
    if (sys->DispStatRW7.HBlankIRQ) Console_ScheduleIRQs(sys, IRQ_HBlank, false, now+2); // CHECKME: delay?

    // schedule hblank
    Schedule_Event(sys, LCD_Scanline, Evt_Scanline, now + (HBlank_Cycles*2));
}

void LCD_Scanline(struct Console* sys, timestamp now)
{
    sys->VCount++;
    sys->VCount %= 263;
    //ARM9_Log(&sys->ARM9);
    //ARM7_Log(&sys->ARM7);

    // check for vblank; clear hblank.
    // this occurs before vcount writes
    if (sys->VCount == 192)
    {
        if (SDL_GetGamepadButton(sys->Pad, SDL_GAMEPAD_BUTTON_LEFT_STICK))
        {
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
        }
#ifdef MonitorFPS
        u64 newtime = SDL_GetPerformanceCounter();
        double time = ((double)(newtime-sys->OldTime) / SDL_GetPerformanceFrequency()) * 1000.0;
        //if (time > 0.3)
            printf("%f\n", time);
        sys->OldTime = newtime;
#endif
        sys->DispStatRO9.Raw = 0b001;
        sys->DispStatRO7.Raw = 0b001;

        // schedule irq
        if (sys->DispStatRW9.VBlankIRQ) Console_ScheduleIRQs(sys, IRQ_VBlank, true, now+2);
        if (sys->DispStatRW7.VBlankIRQ) Console_ScheduleIRQs(sys, IRQ_VBlank, false, now+2); // CHECKME: delay correct for arm7 too?
        StartDMA9(sys, now+2+1, DMAStart_VBlank); // checkme: delay?
        StartDMA9(sys, now+2+1, DMAStart_VBlank); // checkme: delay?

        GX_Swap(sys, now);
    }
    else if (sys->VCount == 262)
    {
        sys->DispStatRO9.Raw = 0b000;
        sys->DispStatRO7.Raw = 0b000;
    }
    else
    {
        // just clear hblank
        sys->DispStatRO9.HBlank = false;
        sys->DispStatRO7.HBlank = false;
    }

    if (sys->VCount == 214)
    {
        SWRen_RasterizerFrame(sys);
    }

    // todo: vcount write
    if (sys->VCountUpdate)
    {
        sys->VCount = sys->VCountNew;
        sys->VCountUpdate = false;
    }

    // vcount match
    sys->DispStatRO7.VCountMatch = (sys->TargetVCount7 == sys->VCount);
    if (sys->TargetVCount7 == sys->VCount) Console_ScheduleIRQs(sys, IRQ_VCount, false, now+2); // checkme: delay?
    sys->DispStatRO9.VCountMatch = (sys->TargetVCount9 == sys->VCount);
    if (sys->TargetVCount9 == sys->VCount) Console_ScheduleIRQs(sys, IRQ_VCount, true, now+2); // checkme: delay?

    // schedule hblank
    Schedule_Event(sys, LCD_HBlank, Evt_Scanline, now + (ActiveRender_Cycles*2));
}
