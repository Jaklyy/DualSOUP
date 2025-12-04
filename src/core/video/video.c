#include <SDL3/SDL_timer.h>
#include "video.h"
#include "../bus/io.h"
#include "../scheduler.h"
#include "../console.h"
#include "../utils.h"
#include "ppu.h"




void LCD_HBlank(struct Console* sys, timestamp now)
{
    // set hblank flag
    sys->DispStatRO.HBlank = true;
    if (sys->VCount < 192)
    {
        PPU_RenderScanline(sys, false, sys->VCount);
        PPU_RenderScanline(sys, true, sys->VCount);
    }
    // schedule hblank
    Schedule_Event(sys, LCD_Scanline, Sched_Scanline, now + (HBlank_Cycles*2));
}

void LCD_Scanline(struct Console* sys, timestamp now)
{
    sys->VCount++;
    sys->VCount %= 263;

    // check for vblank; clear hblank.
    // this occurs before vcount writes
    if (sys->VCount == 192)
    {
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
    Schedule_Event(sys, LCD_HBlank, Sched_Scanline, now + (ActiveRender_Cycles*2));
}
