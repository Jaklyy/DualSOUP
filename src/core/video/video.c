#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_gamepad.h>
#include "video.h"
#include "core/scheduler.h"
#include "core/console.h"
#include "core/io/dma.h"
#include "core/utils.h"
#include "ppu.h"
#include "3d.h"




void PPU_SetTarget(Console* sys, const timestamp now)
{
#ifndef SINGLETHREADRASTER
    if (sys->PPUTarget < now)
        sys->PPUTarget = now;
#else
    PPU_RenderScanline(sys, false, sys->VCount);
    PPU_RenderScanline(sys, true, sys->VCount);
#endif
}

void PPU_Sync(Console* sys, timestamp now)
{
#ifndef SINGLETHREADRASTER
    if (!sys->PPUStart) return;
    PPU_SetTarget(sys, now);
    while ((sys->PPUATimestamp < now) || (sys->PPUBTimestamp < now)) SDL_CPUPauseInstruction();
#endif
}

void PPU_Wait(Console* sys, const timestamp now)
{
#ifndef SINGLETHREADRASTER
    while (now >= sys->PPUTarget) SDL_CPUPauseInstruction();
#endif
}

void PPU_Init(Console* sys, const timestamp now)
{
#ifndef SINGLETHREADRASTER
    if (sys->PPUStart) return;
    sys->PPUTarget = now;
    sys->PPUATimestamp = now;
    sys->PPUBTimestamp = now;
    sys->PPUStart = true;
#endif
}

void LCD_HBlank(Console* sys, timestamp now)
{
    // set hblank flag
    sys->DispStatRO9.HBlank = true;
    sys->DispStatRO7.HBlank = true;
    if (sys->VCount == 262) 
        PPU_SetTarget(sys, now);
    if (sys->VCount < 192)
    {
        StartDMA9(sys, now+DSClk33(2+1), DMAStart_HBlank); // checkme: delay?
        PPU_SetTarget(sys, now);
    }
    if (sys->VCount == 191)
    {
        PPU_Sync(sys, now);
        sys->RenderedLines = 0;
        // swap buffers
        bool backbuf = sys->BackBuf;
        SDL_LockMutex(sys->FrameBufferMutex[!backbuf]);
        sys->BackBuf = !sys->BackBuf;
        SDL_UnlockMutex(sys->FrameBufferMutex[backbuf]);
        Sched_AddEvent(sys, now, Evt_EndFrame);
    }
    // schedule irq
    if (sys->DispStatRW9.HBlankIRQ) Sched_AddEvent(sys, now, Evt_IRQ9_HBlank); // CHECKME: delay?
    if (sys->DispStatRW7.HBlankIRQ) Sched_AddEvent(sys, now, Evt_IRQ7_HBlank); // CHECKME: delay?

    // schedule scanline start
    Sched_AddEvent(sys, now + DSClk33(HBlank_Cycles), Evt_Scanline);
}

void LCD_Scanline(Console* sys, timestamp now)
{
    sys->VCount++;
    sys->VCount &= 0x1FF;
    if (sys->VCount == 263) sys->VCount = 0;

    // certain ppu state is stepped every scanline.
    // TODO: should this actually be done on the ppu thread...?
    PPU_GlobalStep(sys, now, sys->VCount);

    // check for vblank; clear hblank.
    // this occurs before vcount writes
    if (sys->VCount == 192)
    {
        sys->DispStatRO9.Raw = 0b001;
        sys->DispStatRO7.Raw = 0b001;

        // schedule irq
        if (sys->DispStatRW9.VBlankIRQ) Sched_AddEvent(sys, now+DSClk33(2), Evt_IRQ9_VBlank);
        if (sys->DispStatRW7.VBlankIRQ) Sched_AddEvent(sys, now+DSClk33(2), Evt_IRQ7_VBlank); // CHECKME: delay correct for arm7 too?
        StartDMA9(sys, now+DSClk33(2+1), DMAStart_VBlank); // checkme: delay?
        StartDMA9(sys, now+DSClk33(2+1), DMAStart_VBlank); // checkme: delay?

#ifndef SINGLETHREADRASTER
        SWRen_Sync(sys, now);
#endif
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
#ifdef SINGLETHREADRASTER
        SWRen_RasterizerFrame(sys);
#else
        SWRen_Init(sys, now);
        SWRen_SetTarget(sys, now+DSClk33(Scanline_Cycles*263));
#endif
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
    if (sys->DispStatRW7.VCountMatchIRQ && (sys->TargetVCount7 == sys->VCount)) Sched_AddEvent(sys, now+DSClk33(2), Evt_IRQ7_VCount); // checkme: delay?
    sys->DispStatRO9.VCountMatch = (sys->TargetVCount9 == sys->VCount);
    if (sys->DispStatRW9.VCountMatchIRQ && (sys->TargetVCount9 == sys->VCount)) Sched_AddEvent(sys, now+DSClk33(2), Evt_IRQ9_VCount); // checkme: delay?

    // schedule hblank
    Sched_AddEvent(sys, now + DSClk33(ActiveRender_Cycles), Evt_HBlank);
}
