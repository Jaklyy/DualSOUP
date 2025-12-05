#include <SDL3/SDL_timer.h>
#include <SDL3/SDL.h>
#include "video.h"
#include "../bus/io.h"
#include "../scheduler.h"
#include "../console.h"
#include "../io/dma.h"
#include "../utils.h"
#include "ppu.h"



void LCD_HBlank(struct Console* sys, timestamp now)
{
    // set hblank flag
    sys->DispStatRO.HBlank = true;
    if (sys->VCount < 192)
    {
        StartDMA9(sys, now+2+1, DMAStart_HBlank); // checkme: delay?
        PPU_RenderScanline(sys, false, sys->VCount);
        PPU_RenderScanline(sys, true, sys->VCount);
    }
    // schedule irq
    if (sys->DispStatRW.HBlankIRQ)
    {
        Console_ScheduleIRQs(sys, IRQ_HBlank, true, now+2); // CHECKME: delay?
        Console_ScheduleIRQs(sys, IRQ_HBlank, false, now+2); // CHECKME: delay?
    }
    // schedule hblank
    Schedule_Event(sys, LCD_Scanline, Evt_Scanline, now + (HBlank_Cycles*2));
}

void LCD_Scanline(struct Console* sys, timestamp now)
{
    sys->VCount++;
    sys->VCount %= 263;

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
        sys->DispStatRO.Raw = 0b001;
        // schedule irq
        if (sys->DispStatRW.VBlankIRQ)
        {
            Console_ScheduleIRQs(sys, IRQ_VBlank, true, now+2);
            Console_ScheduleIRQs(sys, IRQ_VBlank, false, now+2); // CHECKME: delay correct for arm7 too?
        }
        StartDMA9(sys, now+2+1, DMAStart_VBlank); // checkme: delay?
        StartDMA9(sys, now+2+1, DMAStart_VBlank); // checkme: delay?
    }
    else if (sys->VCount == 262)
    {
        sys->DispStatRO.Raw = 0b000;
    }
    else sys->DispStatRO.HBlank = false; // just clear hblank

    // todo: vcount write
    if (sys->VCountUpdate)
    {
        sys->VCount = sys->VCountNew;
    }

    // vcount match
    sys->DispStatRO.VCountMatch = (sys->TargetVCount == sys->VCount);

    // schedule hblank
    Schedule_Event(sys, LCD_HBlank, Evt_Scanline, now + (ActiveRender_Cycles*2));
}
