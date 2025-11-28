#include "ppu.h"
#include "../console.h"




u32 RGB565to666(u16 color)
{
    return (((u32)color & 0x1F) << 1) // r
        | ((((u32)color >> 5) & 0x1F) << 7) // g
        | ((((u32)color >> 15) & 0x1) << 6) // g lo
        | ((((u32)color >> 10) & 0x1F) << 13); // b
}

u32 RGB555to666(u16 color)
{
    return (((u32)color & 0x1F) << 1) // r
        | ((((u32)color >> 5) & 0x1F) << 7) // g
        | ((((u32)color >> 10) & 0x1F) << 13); // b
}

u32 VRAM_LCD(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings);
u32 VRAM_BGB(struct Console* sys, const u32 addr, const u32 mask, const bool seq, const bool write, const u32 val, const bool timings);

void PPU_RenderScanline(struct Console* sys, bool B, const u16 y)
{
    struct PPU* ppu = ((B) ? &sys->PPU_B : &sys->PPU_A);

    switch(ppu->DisplayCR.DisplayMode)
    {
        case 0:
            for (int x = 0; x < 256; x++)
            {
                sys->Framebuffer[B][y][x] = 0x3FFFF;
            }
            return;
        case 1:
            break;
        case 2:
        {
            u32 addr = ppu->DisplayCR.VRAMSel * KiB(128);
            for (int x = 0; x < 256; x++)
            {
                sys->Framebuffer[B][y][x] = RGB555to666(VRAM_LCD(sys, addr, 0xFFFFFFFF, false, false, 0, false) >> ((addr & 2)*16));
                addr += 2;
            }
            return;
        }
        case 3:
            LogPrint(LOG_PPU|LOG_UNIMP, "INVALID PPU MODE 3\n");
            return;
    }

    u16* palbase = &sys->Palette.b16[0x400/2];
    u32 bgcol = RGB565to666(palbase[0]);

    u32 tilebase = ppu->BGCR[0].CharBase * KiB(16);
    u32 screenbase = ppu->BGCR[0].ScreenBase * KiB(2);

    //u16 x = 0;

    for (int x = 0; x < 256; x++)
    {
        u32 tileaddr = screenbase+((x / 8) * 2);
        union TextTileData tile = {.Raw = VRAM_BGB(sys, tileaddr, 0xFFFFFFFF, false, false, 0, false) >> ((tileaddr & 2)*16)};
        u16* pal = palbase + tile.Palette;

        u32 pixeladdr = tilebase + (tile.TileNum * 32) + ((x%8) / 2);
        u8 color = VRAM_BGB(sys, pixeladdr, 0xFFFFFFFF, false, false, 0, false) >> ((pixeladdr&3)*8);

        color = (color >> ((x&1)*4)) & 0xF;

        sys->Framebuffer[B][y][x] = RGB565to666(pal[color]);
    }
}
