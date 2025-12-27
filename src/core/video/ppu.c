#include <stdckdint.h>
#include "../console.h"
#include "video.h"
#include "ppu.h"




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
extern u32 VRAM_OBJB(struct Console* sys, const u32 addr, const u32 mask, const bool write, const u32 val, const bool timings);
extern u32 VRAM_OBJA(struct Console* sys, const u32 addr, const u32 mask, const bool write, const u32 val, const bool timings);

u16 VRAM_BGAExtPal(struct Console* sys, const u16 idx)
{
    u16 val = 0;
    if (sys->VRAMCR[4].Raw == 0x84)
    {
        val = sys->VRAM_E.b16[idx];
    }
    if ((sys->VRAMCR[5].Raw & 0x87) == 0x84)
    {
        if ((sys->VRAMCR[5].Offset * KiB(16)) == (idx & KiB(16)))
        {
            val |= sys->VRAM_F.b16[idx & (VRAM_F_Size-1)];
        }
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x84)
    {
        if ((sys->VRAMCR[6].Offset * KiB(16)) == (idx & KiB(16)))
        {
            val |= sys->VRAM_G.b16[idx & (VRAM_G_Size-1)];
        }
    }
    return val;
}

u16 VRAM_BGBExtPal(struct Console* sys, const u16 idx)
{
    if (sys->VRAMCR[7].Raw == 0x82)
    {
        return sys->VRAM_H.b16[idx];
    }
    else return 0;
}

u16 VRAM_OBJAExtPal(struct Console* sys, const u16 idx)
{
    u16 val = 0;
    if ((sys->VRAMCR[5].Raw & 0x87) == 0x85)
    {
        //if ((sys->VRAMCR[5].Offset * KiB(16)) == (idx & KiB(16))) checkme?
        {
            val = sys->VRAM_F.b16[idx & (VRAM_F_Size-1)];
        }
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x85)
    {
        //if ((sys->VRAMCR[6].Offset * KiB(16)) == (idx & KiB(16))) checkme?
        {
            val |= sys->VRAM_G.b16[idx & (VRAM_G_Size-1)];
        }
    }
    return val;
}

u16 VRAM_OBJBExtPal(struct Console* sys, const u16 idx)
{
    if (sys->VRAMCR[8].Raw == 0x83)
    {
        return sys->VRAM_I.b16[idx & (VRAM_I_Size-1)];
    }
    else return 0;
}


void PPU_None(struct Console* sys, const bool b, const u8 bg)
{
    CompositeBuffer* buffer = (b ? sys->CompositeBufferB[bg] : sys->CompositeBufferA[bg]);
    for (int x = 0; x < 256; x++)
        buffer[x] = (CompositeBuffer){0, 0, true, false, false, false};
}

void PPU_RenderText(struct Console* sys, const bool b, u16 y, const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    u32 (*BG)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_BGB : VRAM_BGA);
    CompositeBuffer* buffer = (b ? sys->CompositeBufferB[bg] : sys->CompositeBufferA[bg]);

    u32 tilebase = ppu->BGCR[bg].CharBase * KiB(16);
    u32 screenbase = ppu->BGCR[bg].ScreenBase * KiB(2);

    if (!b)
    {
        tilebase += ppu->DisplayCR.CharBase * KiB(64);
        screenbase += ppu->DisplayCR.ScreenBase * KiB(64);
    }

    y += ppu->Yoff[bg];

    // screens are laid out somewhat unintuitively in vram so we need to do some weird things to index them properly
    if (ppu->BGCR[bg].Tall)
    {
        // 512 height
        screenbase += ((y & (512-1)) / TileWidth) * (ScreenWidthTiles * BytesPerTile);
        // if it is also wide then we have to offset the height some to get the proper screen offset if it's past the 256th pixel.
        if (ppu->BGCR[bg].Wide) screenbase += ((y / ScreenWidthPx * ScreenWidthPx) / TileWidth) * (ScreenWidthTiles * BytesPerTile);
    }
    else
    {
        // 256 height
        screenbase += ((y & (256-1)) / TileWidth) * (ScreenWidthTiles * BytesPerTile);
    }


    u32 tileaddr;
    TextTileData tile;
    //u16* pal = nullptr;
    // isolate out msb since it needs to be special cased for properly indexing screens
    u8 x = ppu->Xoff[bg] & 0xFF;
    bool xmsb = (ppu->Xoff[bg] >> 8) & ppu->BGCR[bg].Wide;
    for (int xf = 0; xf < 256; xf++)
    {
        if (((x%8) == 0) || (xf == 0))
        {
            tileaddr = screenbase + ((x / 8) * 2) + (xmsb << (8+3));
            tile.Raw = (BG(sys, tileaddr&~3, u32_max, false, 0, false) >> ((tileaddr & 2)*8)) & 0xFFFF;
        }

        // get in tile coordinate component to index into the tiles
        int xfrac = (!tile.HFlip ? (x%8) : (7-(x%8)));
        int yfrac = (!tile.VFlip ? (y%8) : (7-(y%8)));

        if (ppu->BGCR[bg].Pal256) // 8 bpp
        {
            u32 pixeladdr = tilebase + (((tile.TileNum * (TileWidth*TileHeight)) + (yfrac*TileWidth)) + xfrac);
            u8 idx = BG(sys, pixeladdr&~3, u32_max, false, 0, false) >> ((pixeladdr&3)*8);

            buffer[xf] = (CompositeBuffer){idx, 0, !idx, false, ppu->DisplayCR.BGExtPalEn, false};
        }
        else // pal 16 4bpp
        {
            u32 pixeladdr = tilebase + ((((tile.TileNum * (TileWidth*TileHeight)) + (yfrac*TileWidth)) / 2) + (xfrac/2));
            u8 idx = BG(sys, pixeladdr&~3, u32_max, false, 0, false) >> ((pixeladdr&3)*8);
            idx = ((idx >> ((xfrac&1)*4)) & 0xF);

            // ext pal doesn't apply for 4bpp tilesets for w/e reason
            buffer[xf] = (CompositeBuffer){idx+(tile.Palette*16), 0, !idx, false, false, false};
        }

        xmsb ^= ckd_add(&x, x, 1) & ppu->BGCR[bg].Wide;
    }
}

void PPU_RenderBitmap(struct Console* sys, const bool b, u16 y, const u8 bg, const bool dircolor)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    u32 (*BG)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_BGB : VRAM_BGA);
    CompositeBuffer* buffer = (b ? sys->CompositeBufferB[bg] : sys->CompositeBufferA[bg]);

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
            buffer[x] = (CompositeBuffer){color&0x7FFF, 0, !(color & 0x8000), true, false, false};
        }
        else
        {
            u32 addr = screenbase + x + (y*width);
            u8 idx = BG(sys, addr&~3, u32_max, false, 0, false) >> ((addr & 3) * 8);
            buffer[x] = (CompositeBuffer){idx, 0, !idx, false, false, false};
        }
    }
}

void PPU_Affine(struct Console* sys, const bool b, const u16 y, const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: AFFINE BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
}

void PPU_Extended(struct Console* sys, const bool b, const u16 y, const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);

    if (ppu->BGCR[bg].Pal256)
    {
        PPU_RenderBitmap(sys, b, y, bg, ppu->BGCR[bg].CharBase & 1);
    }
    else
    {
        PPU_None(sys, b, bg);
        LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: AFFINE/TEXT BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
    }
}

void PPU_Large(struct Console* sys, const bool b, const u16 y, const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = (b ? sys->CompositeBufferB[bg] : sys->CompositeBufferA[bg]);
    PPU_None(sys, b, bg);
    LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: LARGE BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
}

void PPU_3D(struct Console* sys, const u16 y)
{
    //PPU* ppu = &sys->PPU_A;
    CompositeBuffer* buffer = sys->CompositeBufferA[0];

    for (int x = 0; x < 256; x++)
        buffer[x] = (CompositeBuffer){sys->GX3D.CBuf[0][y][x] & 0x3FFFF, 0, !(sys->GX3D.CBuf[0][y][x] >> 18) /* TODO */, true, false, true};
}

void PPU_BG0_Lookup(struct Console* sys, const bool b, const u16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG0Enable) return PPU_None(sys, b, 0);
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0 ... 5:
            if (ppu->DisplayCR.BG03D && !b)
            {
                PPU_3D(sys, y);
                break;
            }
            [[fallthrough]];
        case 7:
            PPU_RenderText(sys, b, y, 0);
            break;

        case 6:
            if (!b)
            {
                PPU_3D(sys, y); // wtf happens to engine b?
            }
            break;
    }
}

void PPU_BG1_Lookup(struct Console* sys, const bool b, const u16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG1Enable) return PPU_None(sys, b, 1);
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0 ... 5:
        case 7:
            return PPU_RenderText(sys, b, y, 1);

        case 6:
            return PPU_None(sys, b, 1);

    }
}

void PPU_BG2_Lookup(struct Console* sys, const bool b, const u16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG2Enable) return PPU_None(sys, b, 2);
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0 ... 1:
        case 3:
            return PPU_RenderText(sys, b, y, 2);

        case 2:
        case 4:
            return PPU_Affine(sys, b, y, 2);

        case 5:
            return PPU_Extended(sys, b, y, 2);

        case 6:
            return PPU_Large(sys, b, y, 2);

        case 7:
            return PPU_None(sys, b, 2);

    }
}

void PPU_BG3_Lookup(struct Console* sys, const bool b, const u16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG3Enable) return PPU_None(sys, b, 3);
    switch(ppu->DisplayCR.BGSetup)
    {
        case 0:
            return PPU_RenderText(sys, b, y, 3);

        case 1 ... 2:
            return PPU_Affine(sys, b, y, 3);

        case 3 ... 5:
            return PPU_Extended(sys, b, y, 3);

        case 6 ... 7:
            return PPU_None(sys, b, 3);
    }
}


void PPU_BuildBGs(struct Console* sys, const bool b, const u16 y)
{

    // todo: windows

    // sprite mosaic???
    PPU_BG0_Lookup(sys, b, y);
    PPU_BG1_Lookup(sys, b, y);
    PPU_BG2_Lookup(sys, b, y);
    PPU_BG3_Lookup(sys, b, y);
}

void PPU_Composite(struct Console* sys, const bool b, const u16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    volatile u16* palbase = (b ? &sys->Palette.b16[0x400/sizeof(u16)] : &sys->Palette.b16[0]);
    u16 (*BGExtPal)(struct Console*, const u16) = (b ? VRAM_BGBExtPal : VRAM_BGAExtPal);
    u16 (*OBJExtPal)(struct Console*, const u16) = (b ? VRAM_OBJBExtPal : VRAM_OBJAExtPal);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? b : !b][y];
    volatile timestamp* time = (b ? (&sys->PPUBTimestamp) : (&sys->PPUATimestamp));

    for (int x = 0; x < 256; x++)
    {
        bool spr = false;
        int bg;
        // initialize with bg color
        CompositeBuffer index = (CompositeBuffer){0, 0, false, false, false /* checkme? */, false};
        for (int prio = 0; prio < 4; prio++)
        {
            // check if sprites should be rendered
            CompositeBuffer tmp = (b ? sys->CompositeBufferB[4][x] : sys->CompositeBufferA[4][x]);
            if (!tmp.Empty && (tmp.SprPrio == prio))
            {
                spr = true;
                index = tmp;
                goto exit;
            }

            // check bgs
            for (bg = 0; bg < 4; bg++)
            {
                // check priority
                if (ppu->BGCR[bg].BGPriority != prio) continue;

                // check if bg exists
                tmp = (b ? sys->CompositeBufferB[bg][x] : sys->CompositeBufferA[bg][x]);
                if (!tmp.Empty)
                {
                    index = tmp;
                    goto exit;
                }
            }
        }
        exit:

        u16 color;
        if (index.NotPal) color = index.Index;
        else if (index.ExtPal)
        {
            // TODO: handle sprites
            if (spr)
            {
                color = OBJExtPal(sys, index.Index);
            }
            else
            {
                u32 extpalbase = bg*KiB(8);
                if ((bg <= 1) && ppu->BGCR[bg].ExtPalSlot) extpalbase += KiB(16);
                color = BGExtPal(sys, index.Index+extpalbase);
            }
        }
        else
        {
            AddBusContention(sys->AHBBusyTS, *time, Dev_Palette);
            color = palbase[index.Index + ((spr) ? 0x100 : 0)];
        }
        scanline[x] = (index.GPU3D ? color : RGB565to666(color));

        *time += 6;
        PPU_Wait(sys, *time);
    }
    *time += 2+HBlank_Cycles;
}

void PPU_SpriteNormal(struct Console* sys, const bool b, const SprAttrs01 attr1, const SprAttrs2 attr2, const u8 width, const u8 height, u8 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = (b ? sys->CompositeBufferB[4] : sys->CompositeBufferA[4]);
    u32 (*OBJ)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_OBJB : VRAM_OBJA);

    if (attr1.VFlip) y = (height - 1 - y);

    s16 x = attr1.X;
    s16 xend = x + width;
    s16 xmod = (x < 0) ? 0-x : 0;
    if (x < 0) x = 0;

    if (attr1.Mode == 3)
    {
        LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: BITMAP SPRITES\n");
    }
    else if (attr1.Mode == 2)
    {
        LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: WINDOW SPRITES\n");
    }
    else
    {
        u32 baseaddr = attr2.TileNum;
        if (ppu->DisplayCR.TileOBJ1D)
        {
            baseaddr <<= ppu->DisplayCR.TileOBJ1DBound;
            baseaddr += ((y/8) * (width/8)) << attr1.Pal256;
        }
        else
        {
            baseaddr += (y/8)*32;
        }

        if (attr1.Pal256)
        {
            baseaddr = (baseaddr * 32) + ((y%8) * 8);

            for (;x < xend && x < 256; x++, xmod++)
            {
                u32 addr = baseaddr + (attr1.HFlip ? (((width-1-xmod)/8*8*8) + ((width-1-xmod)%8)) : ((xmod/8*8*8) + (xmod%8)));
                u8 index = OBJ(sys, addr&~3, 0xFFFFFFFF, false, 0, false) >> ((addr&3)*8);
                if (index && (buffer[x].Empty || (buffer[x].SprPrio > attr2.Priority))) buffer[x] = (CompositeBuffer){index, attr2.Priority, false, false, ppu->DisplayCR.SprExtPalEn, false};
            }
        }
        else
        {
            baseaddr = (baseaddr * 32) + ((y%8) * 4);

            for (;x < xend && x < 256; x++, xmod++)
            { 
                u32 addr = baseaddr + (attr1.HFlip ? (((width-1-xmod)/8*8*4) + ((width-1-xmod)%8/2)) : ((xmod/8*8*4) + (xmod%8/2)));
                u8 index = OBJ(sys, addr&~3, 0xFFFFFFFF, false, 0, false) >> ((addr&3)*8);
                index = ((index >> (((xmod&1)^attr1.HFlip)*4)) & 0xF);
                if (index && (buffer[x].Empty || (buffer[x].SprPrio > attr2.Priority))) buffer[x] = (CompositeBuffer){index+(attr2.PaletteOffset<<4), attr2.Priority, false, false, false, false};
            }
        }
    }
}

void PPU_BuildSprites(struct Console* sys, const bool b, const u8 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    volatile u32* oambase = (b ? &sys->OAM.b32[0x400/sizeof(u32)] : &sys->OAM.b32[0]);

    PPU_None(sys, b, 4); // clear sprite buffer
    if (!ppu->DisplayCR.SprEnable) return;

    for (int spr = 0; spr < 128; spr++)
    {
        SprAttrs01 attr = {.Raw = oambase[spr*2]};

        if (attr.RotScal)
        {
            LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: AFFINE SPRITES\n");
        }
        else
        {
            if (attr.Disable) continue;

            // step 1: calc sprite y bounds
            u8 width = 8 << attr.Size;
            u8 height = 8 << attr.Size;

            if (attr.Shape == 1)
            {
                height >>= 1;
                if (height <= 8)
                {
                    height = 8;
                    width <<= 1;
                }
            }
            else if (attr.Shape == 2)
            {
                width >>= 1;
                if (width <= 8)
                {
                    width = 8;
                    height <<= 1;
                }
            }
            else if (attr.Shape == 3) // checkme?
            {
                width = 8;
                height = 8;
            }

            u8 sy = y - attr.Y & 0xFF;
            if (sy >= height) continue;

            // checkme: does it skip sprites based on x coordinate?

            // fetch attribute 2 now that we know this sprite will render
            SprAttrs2 attr2 = {.Raw = (oambase[(spr*2)+1] & 0xFFFF)};

            PPU_SpriteNormal(sys, b, attr, attr2, width, height, sy);
        }
    }
}

void PPU_RenderScanline(struct Console* sys, const bool b, const s16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? b : !b][y];
    volatile timestamp* time = (b ? (&sys->PPUBTimestamp) : (&sys->PPUATimestamp));

    if (y >= 0)
    {
        switch(ppu->DisplayCR.DisplayMode)
        {
            case 0:
                *time += 1538 + HBlank_Cycles;
                for (int x = 0; x < 256; x++)
                    scanline[x] = 0x3FFFF;
                return;
            case 1:
                break;
            case 2:
            {
                u32 addr = (ppu->DisplayCR.VRAMSel * KiB(128)) + (256*2*y);
                *time += 1538 + HBlank_Cycles;
                PPU_Wait(sys, *time);
                for (int x = 0; x < 256; x++)
                {
                    scanline[x] = RGB555to666(VRAM_LCD(sys, addr&~3, u32_max, false, 0, false) >> ((addr & 2)*8));
                    addr += 2;
                }
                return;
            }
            case 3:
                LogPrint(LOG_PPU|LOG_UNIMP, "INVALID PPU MODE 3\n");
                *time += 1538 + HBlank_Cycles;
                return;
        }

        if (ppu->DisplayCR.ForceBlank)
        {
            *time += 1538 + HBlank_Cycles;
            for (int x = 0; x < 256; x++)
                scanline[x] = 0x3FFFF;
            return;
        }

        PPU_BuildBGs(sys, b, y);
        PPU_Composite(sys, b, y);
    }
    if (y < 191)
    {
        if (y == -1)
            *time += 1538 + HBlank_Cycles;
        PPU_BuildSprites(sys, b, y+1);
    }
}

int PPUA_MainLoop(void* ptr)
{
    struct Console* sys = ptr;
    while (!sys->PPUStart) thrd_yield();

    while (!sys->KillPPUs)
    {
        for (int y = -1; y < 192; y++)
        {
            sys->PPUATimestamp += 46;
            PPU_Wait(sys, sys->PPUATimestamp);
            PPU_RenderScanline(sys, false, y);
        }
        sys->PPUATimestamp += Scanline_Cycles*70;
    }
    return 0;
}

int PPUB_MainLoop(void* ptr)
{
    struct Console* sys = ptr;
    while (!sys->PPUStart) thrd_yield();

    while (!sys->KillPPUs)
    {
        for (int y = -1; y < 192; y++)
        {
            sys->PPUBTimestamp += 46;
            PPU_Wait(sys, sys->PPUBTimestamp);
            PPU_RenderScanline(sys, true, y);
        }
        sys->PPUBTimestamp += Scanline_Cycles*70;
    }
    return 0;
}
