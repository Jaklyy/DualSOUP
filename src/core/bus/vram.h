#pragma once
#include "core/console.h"




u16 VRAM_LCD(Console* sys, const u32 addr);
u16 VRAM_BGA(Console* sys, const u32 addr);
u16 VRAM_OBJA(Console* sys, const u32 addr);
u16 VRAM_BGB(Console* sys, const u32 addr);
u16 VRAM_OBJB(Console* sys, const u32 addr);
u16 VRAM_ARM7(Console* sys, const u32 addr);
u16 VRAM_BGAExtPal(Console* sys, const u32 addr);
u16 VRAM_BGBExtPal(Console* sys, const u32 addr);
u16 VRAM_OBJAExtPal(Console* sys, const u32 addr);
u16 VRAM_OBJBExtPal(Console* sys, const u32 addr);
