#pragma once
#include "../utils.h"





struct Console;

void PPU_SetTarget(struct Console* sys, const timestamp now);
void PPU_Sync(struct Console* sys, timestamp now);
void PPU_Wait(struct Console* sys, const timestamp now);

void LCD_HBlank(struct Console* sys, timestamp now);
void LCD_Scanline(struct Console* sys, timestamp now);
