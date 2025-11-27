#include "video.h"
#include "scheduler.h"
#include "console.h"
#include "utils.h"




void LCD_Scanline(struct Console* sys)
{
    sys->VCount++;
    sys->VCount %= 262;
    Schedule_Event(sys, LCD_Scanline, Sched_Scanline, Scanline_Cycles*2, true);
}
