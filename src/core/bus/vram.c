#include "core/utils.h"
#include "core/console.h"




// Thanks to Arisotura for some notes on vram bank mirroring.
// I never would've guessed that they mirror so weirdly within a given region.
u16 VRAM_LCD(Console* sys, const u32 addr)
{
    switch((addr >> 12) & 0xFC)
    {
    case 0x00 ... 0x1C: return ((sys->VRAMCR[VRAMID_A].Raw & 0x87) == 0x80) ? (1<<VRAMID_A) : 0;
    case 0x20 ... 0x3C: return ((sys->VRAMCR[VRAMID_B].Raw & 0x87) == 0x80) ? (1<<VRAMID_B) : 0;
    case 0x40 ... 0x5C: return ((sys->VRAMCR[VRAMID_C].Raw & 0x87) == 0x80) ? (1<<VRAMID_C) : 0;
    case 0x60 ... 0x7C: return ((sys->VRAMCR[VRAMID_D].Raw & 0x87) == 0x80) ? (1<<VRAMID_D) : 0;
    case 0x80 ... 0x8C: return ((sys->VRAMCR[VRAMID_E].Raw & 0x87) == 0x80) ? (1<<VRAMID_E) : 0;
    case 0x90:          return ((sys->VRAMCR[VRAMID_F].Raw & 0x87) == 0x80) ? (1<<VRAMID_F) : 0;
    case 0x94:          return ((sys->VRAMCR[VRAMID_G].Raw & 0x87) == 0x80) ? (1<<VRAMID_G) : 0;
    case 0x98 ... 0x9C: return ((sys->VRAMCR[VRAMID_H].Raw & 0x87) == 0x80) ? (1<<VRAMID_H) : 0;
    case 0xA0:          return ((sys->VRAMCR[VRAMID_I].Raw & 0x87) == 0x80) ? (1<<VRAMID_I) : 0;
    default:            return 0;
    }
}

#define VRAMCHECK(id, mode, addrmask, offsmul) \
    if (((sys->VRAMCR[id].Raw & 0x87) == (0x80 | mode)) && ((addr & addrmask) == (sys->VRAMCR[id].Offset * offsmul))) list |= 1<<id;

#define VRAMCHECKSplit(id, mode, addrmask, offs0mul, offs1mul) \
    if (((sys->VRAMCR[id].Raw & 0x87) == (0x80 | mode)) && ((addr & addrmask) == ((sys->VRAMCR[id].Offset & 1) * offs0mul) + ((sys->VRAMCR[id].Offset >> 1) * offs1mul))) list |= 1<<id;

#define VRAMCHECKDirect(id, mode, addrmask, idx) \
    if (((sys->VRAMCR[id].Raw & 0x87) == (0x80 | mode)) && ((addr & addrmask) == idx)) list |= 1<<id;

u16 VRAM_BGA(Console* sys, const u32 addr)
{
    u16 list = 0;
    VRAMCHECK(VRAMID_A, 1, 0x60000, 0x20000)
    VRAMCHECK(VRAMID_B, 1, 0x60000, 0x20000)
    VRAMCHECK(VRAMID_C, 1, 0x60000, 0x20000)
    VRAMCHECK(VRAMID_D, 1, 0x60000, 0x20000)
    VRAMCHECKDirect(VRAMID_E, 1, 0x70000, 0)
    VRAMCHECKSplit(VRAMID_F, 1, 0x74000, 0x4000, 0x10000)
    VRAMCHECKSplit(VRAMID_G, 1, 0x74000, 0x4000, 0x10000)
    return list;
}

u16 VRAM_OBJA(Console* sys, const u32 addr)
{
    u16 list = 0;
    VRAMCHECK(VRAMID_A, 2, 0x20000, 0x20000)
    VRAMCHECK(VRAMID_B, 2, 0x20000, 0x20000)
    VRAMCHECKDirect(VRAMID_E, 2, 0x30000, 0)
    VRAMCHECKSplit(VRAMID_F, 2, 0x34000, 0x4000, 0x10000)
    VRAMCHECKSplit(VRAMID_G, 2, 0x34000, 0x4000, 0x10000)
    return list;
}

u16 VRAM_BGB(Console* sys, const u32 addr)
{
    u16 list = 0;
    VRAMCHECKDirect(VRAMID_C, 4, 0, 0)
    VRAMCHECKDirect(VRAMID_H, 1, 0x8000, 0)
    VRAMCHECKDirect(VRAMID_I, 1, 0x8000, 0x8000)
    return list;
}

u16 VRAM_OBJB(Console* sys, const u32 addr [[maybe_unused]])
{
    u16 list = 0;
    VRAMCHECKDirect(VRAMID_D, 4, 0, 0)
    VRAMCHECKDirect(VRAMID_I, 2, 0, 0)
    return list;
}

u16 VRAM_ARM7(Console* sys, const u32 addr)
{
    u16 list = 0;
    VRAMCHECK(VRAMID_C, 2, 0x20000, 0x20000)
    VRAMCHECK(VRAMID_D, 2, 0x20000, 0x20000)
    return list;
}

#undef VRAMCHECK
#undef VRAMCHECKSplit
#undef VRAMCHECKDirect
