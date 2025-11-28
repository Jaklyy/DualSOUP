#pragma once
#include "../utils.h"
#include "../console.h"


struct Console;

u32 IO7_Read(struct Console* sys, const u32 addr, const u32 mask);
void IO7_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask);
u32 IO9_Read(struct Console* sys, const u32 addr, const u32 mask);
void IO9_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask);
