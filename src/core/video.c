#include "video.h"
#include "bus/io.h"
#include "scheduler.h"
#include "console.h"
#include "utils.h"




void LCD_HBlank(struct Console* sys, timestamp now)
{
    // set hblank flag
    sys->IO.DispStatRO.HBlank = true;

    // schedule hblank
    Schedule_Event(sys, LCD_Scanline, Sched_Scanline, now + HBlank_Cycles*2);
}

void LCD_Scanline(struct Console* sys, timestamp now)
{
    sys->VCount++;
    sys->VCount %= 262;

    // check for vblank; clear hblank.
    // this occurs before vcount writes
    if (sys->VCount == 192)
    {
        sys->IO.DispStatRO.Raw = 0b001;
        // schedule irq
        if (sys->IO.DispStatRW.VBlankIRQ)
        {
            Console_ScheduleIRQs(sys, IRQ_VBlank, true, now+2);
            Console_ScheduleIRQs(sys, IRQ_VBlank, false, now+2); // CHECKME: delay correct for arm7 too?
        }
    }
    else if (sys->VCount == 262)
    {
        sys->IO.DispStatRO.Raw = 0b000;
    }
    else sys->IO.DispStatRO.HBlank = false; // just clear hblank

    // todo: vcount write
    if (sys->IO.VCountUpdate)
    {
        sys->VCount = sys->IO.VCountNew;
    }

    // vcount match
    sys->IO.DispStatRO.VCountMatch = (sys->IO.TargetVCount == sys->VCount);

    // schedule hblank
    Schedule_Event(sys, LCD_HBlank, Sched_Scanline, now + (ActiveRender_Cycles*2));
}
