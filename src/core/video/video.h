#pragma once
#include <SDL3/SDL_thread.h>
#include "core/utils.h"





typedef struct Console Console;

void PPU_SetTarget(Console* sys, const timestamp now);
void PPU_Sync(Console* sys, timestamp now);
void PPU_Wait(Console* sys, const timestamp now);

void LCD_HBlank(Console* sys, timestamp now);
void LCD_Scanline(Console* sys, timestamp now);

void SWRen_Init(Console* sys, const timestamp now);
void SWRen_Sync(Console* sys, timestamp now);
void SWRen_SetTarget(Console* sys, const timestamp now);
int SDLCALL SWRen_MainLoop(void* ptr);
void SWRen_SyncRenderedLines(Console* sys, u8 y);
