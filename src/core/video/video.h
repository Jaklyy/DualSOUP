#pragma once
#include "../utils.h"





struct Console;

void LCD_HBlank(struct Console* sys, timestamp now);
void LCD_Scanline(struct Console* sys, timestamp now);
