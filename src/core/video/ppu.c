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

extern u32 VRAM_LCD(struct Console* sys, const u32 addr, const u32 mask, const bool write, const u32 val, const bool timings);
extern u32 VRAM_BGB(struct Console* sys, const u32 addr, const u32 mask, const bool write, const u32 val, const bool timings);
extern u32 VRAM_BGA(struct Console* sys, const u32 addr, const u32 mask, const bool write, const u32 val, const bool timings);




void PPU_RenderText(struct Console* sys, const bool B, const u16 y, const u8 bg)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    u32 (*BG)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (B ? VRAM_BGB : VRAM_BGA);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? B : !B][y];

    u16* palbase = &sys->Palette.b16[0x200];
    u32 bgcol = RGB565to666(palbase[0]);

    u32 tilebase = ppu->BGCR[bg].CharBase * KiB(16);
    u32 screenbase = ppu->BGCR[bg].ScreenBase * KiB(2);

    switch(ppu->BGCR[bg].ScreenSize)
    {
        case 0: screenbase += ((y / 8)*(256/8)*2); break;
        case 1: screenbase += ((y / 8)*(512/8)*2); break;
        case 2: screenbase += ((y / 8)*(256/8)*2); break;
        case 3: screenbase += ((y / 8)*(512/8)*2); break;
    }
    u32 width;
    switch(ppu->BGCR[bg].ScreenSize)
    {
        case 0: width = 128; break;
        case 1: width = 256; break;
        case 2: width = 512; break;
        case 3: width = 512; break;
    }

    u32 tileaddr;
    union TextTileData tile;
    u16* pal;
    for (int x = 0; x < 256; x++)
    {
        if ((x%8) == 0)
        {
            tileaddr = screenbase+((x / 8) * 2);
            tile.Raw = BG(sys, tileaddr&~3, u32_max, false, 0, false) >> ((tileaddr & 2)*8);
            pal = palbase + (tile.Palette * 16);
        }

        int xfrac = (!tile.HFlip ? (x%8) : (7-(x%8)));
        int yfrac = (!tile.VFlip ? (y%8) : (7-(y%8)));

        if (ppu->BGCR[bg].Pal256)
        {
            u32 pixeladdr = tilebase + (tile.TileNum * 32) + (xfrac) + (yfrac*4);
            u8 color = BG(sys, pixeladdr&~3, u32_max, false, 0, false) >> ((pixeladdr&3)*8);

            scanline[x] = RGB565to666(palbase[color]);
        }
        else
        {
            u32 pixeladdr = tilebase + (tile.TileNum * 32) + (xfrac/2) + (yfrac*4);
            u8 color = BG(sys, pixeladdr&~3, u32_max, false, 0, false) >> ((pixeladdr&3)*8);
            color = (color >> ((xfrac&1)*4)) & 0xF;

            scanline[x] = RGB565to666(pal[color]);
        }
    }
}

void PPU_RenderBitmap(struct Console* sys, const bool B, const u16 y, const u8 bg, const bool dircolor)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    u32 (*BG)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (B ? VRAM_BGB : VRAM_BGA);
    u16* palbase = (B ? &sys->Palette.b16[0x200] : &sys->Palette.b16[0]);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? B : !B][y];

    u32 screenbase = ppu->BGCR[bg].ScreenBase * KiB(16);

    u32 width;
    switch(ppu->BGCR[bg].ScreenSize)
    {
        case 0: width = 128; break;
        case 1: width = 256; break;
        case 2: width = 512; break;
        case 3: width = 512; break;
    }

    for (int x = 0; x < 256; x++)
    {
        if (dircolor)
        {
            u32 addr = screenbase + (x*2) + (y*(width*2));
            u16 color = BG(sys, addr&~3, u32_max, false, 0, false) >> ((addr & 2) * 8);
            if (color & 0x8000)
                scanline[x] = RGB555to666(color);
        }
        else
        {
            u32 addr = screenbase + x + (y*width);
            u8 idx = BG(sys, addr&~3, u32_max, false, 0, false) >> ((addr & 3) * 8);
            scanline[x] = RGB565to666(palbase[idx]);
        }
    }
}

void PPU_Affine(struct Console* sys, const bool B, const u16 y, const u8 bg)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: AFFINE BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
}

void PPU_Extended(struct Console* sys, const bool B, const u16 y, const u8 bg)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);

    if (ppu->BGCR[bg].Pal256)
    {
        PPU_RenderBitmap(sys, B, y, bg, ppu->BGCR[bg].CharBase & 1);
    }
    else PPU_RenderText(sys, B, y, bg); //LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: AFFINE/TEXT BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
}

void PPU_Large(struct Console* sys, const bool B, const u16 y, const u8 bg)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: LARGE BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
}

void PPU_BG0_Lookup(struct Console* sys, const bool B, const u16 y)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG0Enable) return;
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0 ... 5:
            // TODO: 3D?
            if (ppu->DisplayCR.BG03D) return;
            [[fallthrough]];
        case 7:
            return PPU_RenderText(sys, B, y, 0);

        case 6: return; // 3D?
    }
}

void PPU_BG1_Lookup(struct Console* sys, const bool B, const u16 y)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG1Enable) return;
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0 ... 5:
        case 7:
            return PPU_RenderText(sys, B, y, 1);

        case 6: return;
    }
}


void PPU_BG2_Lookup(struct Console* sys, const bool B, const u16 y)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG2Enable) return;
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0 ... 1:
        case 3:
            return PPU_RenderText(sys, B, y, 2);

        case 2:
        case 4:
            return PPU_Affine(sys, B, y, 2);

        case 5:
            return PPU_Extended(sys, B, y, 2);

        case 6:
            return PPU_Large(sys, B, y, 2);

        case 7: return;
    }
}


void PPU_BG3_Lookup(struct Console* sys, const bool B, const u16 y)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG3Enable) return;
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0:
            return PPU_RenderText(sys, B, y, 3);

        case 1 ... 2:
            return PPU_Affine(sys, B, y, 3);

        case 3 ... 5:
            return PPU_Extended(sys, B, y, 3);

        case 6 ... 7: return;
    }
}


void PPU_RenderBG(struct Console* sys, const bool B, const u16 y)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    u16* palbase = (B ? &sys->Palette.b16[0x200] : &sys->Palette.b16[0]);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? B : !B][y];

    if (ppu->DisplayCR.ForceBlank)
    {
        for (int x = 0; x < 256; x++)
            scanline[x] = 0x3FFFF;
        return;
    }

    u32 color = RGB565to666(palbase[0]);
    for (int x = 0; x < 256; x++)
        scanline[x] = color;

    // todo: windows

    // sprite mosaic???
    for (int prio = 3; prio >= 0; prio--)
        for (int bg = 3; bg >= 0; bg--)
        {
            if (ppu->BGCR[bg].BGPriority == prio)
            {
                if (bg == 0) PPU_BG0_Lookup(sys, B, y);
                if (bg == 1) PPU_BG1_Lookup(sys, B, y);
                if (bg == 2) PPU_BG2_Lookup(sys, B, y);
                if (bg == 3) PPU_BG3_Lookup(sys, B, y);
            }
        }
}

void PPU_RenderScanline(struct Console* sys, const bool B, const u16 y)
{
    struct PPU* ppu = (B ? &sys->PPU_B : &sys->PPU_A);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? B : !B][y];

    switch(ppu->DisplayCR.DisplayMode)
    {
        case 0:
            for (int x = 0; x < 256; x++)
                scanline[x] = 0x3FFFF;
            return;
        case 1:
            break;
        case 2:
        {
            u32 addr = (ppu->DisplayCR.VRAMSel * KiB(128)) + (256*2*y);
            for (int x = 0; x < 256; x++)
            {
                scanline[x] = RGB555to666(VRAM_LCD(sys, addr&~3, u32_max, false, 0, false) >> ((addr & 2)*8));
                addr += 2;
            }
            return;
        }
        case 3:
            LogPrint(LOG_PPU|LOG_UNIMP, "INVALID PPU MODE 3\n");
            return;
    }

    PPU_RenderBG(sys, B, y);
}
