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
        val = sys->VRAM_E.b16[idx & ((VRAM_E_Size/sizeof(u16))-1)];
    }
    if ((sys->VRAMCR[5].Raw & 0x87) == 0x84)
    {
        if ((sys->VRAMCR[5].Offset * (KiB(16)/sizeof(u16))) == (idx & (0x4000/sizeof(u16))))
        {
            val |= sys->VRAM_F.b16[idx & ((VRAM_F_Size/sizeof(u16))-1)];
        }
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x84)
    {
        if ((sys->VRAMCR[6].Offset * (KiB(16)/sizeof(u16))) == (idx & (0x4000/sizeof(u16))))
        {
            val |= sys->VRAM_G.b16[idx & ((VRAM_G_Size/sizeof(u16))-1)];
        }
    }
    return val;
}

u16 VRAM_BGBExtPal(struct Console* sys, const u16 idx)
{
    if (sys->VRAMCR[7].Raw == 0x82)
    {
        return sys->VRAM_H.b16[idx & ((VRAM_H_Size/sizeof(u16))-1)];
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
            val = sys->VRAM_F.b16[idx & ((VRAM_F_Size/sizeof(u16))-1)];
        }
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x85)
    {
        //if ((sys->VRAMCR[6].Offset * KiB(16)) == (idx & KiB(16))) checkme?
        {
            val |= sys->VRAM_G.b16[idx & ((VRAM_G_Size/sizeof(u16))-1)];
        }
    }
    return val;
}

u16 VRAM_OBJBExtPal(struct Console* sys, const u16 idx)
{
    if (sys->VRAMCR[8].Raw == 0x83)
    {
        return sys->VRAM_I.b16[idx & ((VRAM_I_Size/sizeof(u16))-1)];
    }
    else return 0;
}


void PPU_None(struct Console* sys, const bool b, const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];
    for (int x = 0; x < 256; x++)
        buffer[x] = (CompositeBuffer){0, 0, true, false, false, false, false};
}

void PPU_RenderText(struct Console* sys, const bool b, u16 y, const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    u32 (*BG)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_BGB : VRAM_BGA);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];

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

            buffer[xf] = (CompositeBuffer){idx+(tile.Palette*256), 0, !idx, false, ppu->DisplayCR.BGExtPalEn, false, false};
        }
        else // pal 16 4bpp
        {
            u32 pixeladdr = tilebase + ((((tile.TileNum * (TileWidth*TileHeight)) + (yfrac*TileWidth)) / 2) + (xfrac/2));
            u8 idx = BG(sys, pixeladdr&~3, u32_max, false, 0, false) >> ((pixeladdr&3)*8);
            idx = ((idx >> ((xfrac&1)*4)) & 0xF);

            // ext pal doesn't apply for 4bpp tilesets for w/e reason
            buffer[xf] = (CompositeBuffer){idx+(tile.Palette*16), 0, !idx, false, false, false, false};
        }

        xmsb ^= ckd_add(&x, x, 1) & ppu->BGCR[bg].Wide;
    }
}

void PPU_RenderBitmap(struct Console* sys, const bool b, u16 y, const u8 bg, const bool dircolor)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    u32 (*BG)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_BGB : VRAM_BGA);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];

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
            buffer[x] = (CompositeBuffer){RGB565to666(color&0x7FFF), 0, !(color & 0x8000), true, false, false, false};
        }
        else // 8 bit index
        {
            u32 addr = screenbase + x + (y*width);
            u8 idx = BG(sys, addr&~3, u32_max, false, 0, false) >> ((addr & 3) * 8);
            buffer[x] = (CompositeBuffer){idx, 0, !idx, false, false, false, false};
        }
    }
}

void PPU_Affine(struct Console* sys, const bool b, const u16 y [[maybe_unused]], const u8 bg)
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

void PPU_Large(struct Console* sys, const bool b, const u16 y [[maybe_unused]], const u8 bg)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    //CompositeBuffer* buffer = (b ? sys->CompositeBufferB[bg] : sys->CompositeBufferA[bg]);
    PPU_None(sys, b, bg);
    LogPrint(LOG_PPU|LOG_UNIMP, "UNIMPLEMENTED: LARGE BG %i %08X\n", bg, ppu->BGCR[bg].Raw);
}

void PPU_3D(struct Console* sys, const u16 y)
{
    CompositeBuffer* buffer = sys->PPU_A.CompositeBuffer[0];
    SWRen_SyncRenderedLines(sys, y+1);
    for (int x = 0; x < 256; x++)
        buffer[x] = (CompositeBuffer){sys->GX3D.CBuf[0][y][x] + (1<<18) /* increase alpha to allow for max alpha for blending */, 0, ((sys->GX3D.CBuf[0][y][x] >> 18) & 0x1F) == 0, true, false, true, true};
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
                PPU_3D(sys, y);
            }
            else PPU_None(sys, b, 0); // wtf happens to engine b?
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

u32 PPU_Blend(PPU* ppu, CompositeBuffer* indices, u32* colors, int* bgs, int num, const bool enabled)
{
    u8 alpA;
    u8 alpB;

    u16 rgb[2][4] = {{colors[0] & 0x3F, (colors[0] >> 6) & 0x3F, (colors[0] >> 12) & 0x3F, 0},
                     {colors[1] & 0x3F, (colors[1] >> 6) & 0x3F, (colors[1] >> 12) & 0x3F, 0}};

    if ((num == 2) && indices[0].ForceBlend && (ppu->BlendCR.BlendBot & (1<<bgs[1])))
    {
        if (indices[0].HasAlpha)
        {
            alpA = (colors[0] >> 18);

            alpB = 32 - alpA;
            goto Alpha_Blend;
        }
        else
        {
            goto Force_Blend;
        }
    }

    if (!enabled || !(ppu->BlendCR.BlendTop & (1<<bgs[0])))
        return colors[0];

    switch(ppu->BlendCR.Effect)
    {
    case BLDCR_Off:
        return colors[0];

    case BLDCR_Blend:
        // there must be a lower pixel and it must be blendable
        if ((num == 1) || !(ppu->BlendCR.BlendBot & (1<<bgs[1])))
            return colors[0];

        Force_Blend:
        alpA = ((ppu->BlendAlpha[0] <= 16) ? ppu->BlendAlpha[0] : 16) << 1;
        alpB = ((ppu->BlendAlpha[1] <= 16) ? ppu->BlendAlpha[1] : 16) << 1;

        Alpha_Blend:
        for (int i = 0; i < 4; i++)
        {
            // rounds to nearest
            rgb[0][i] = ((rgb[0][i] * alpA) + (rgb[1][i] * alpB) + 16) / 32;
            DS_CLAMP(rgb[0][i], >, 0x3F);
        }
        break;

    case BLDCR_Bright:
    {
        u8 bright = ((ppu->BlendBright <= 16) ? ppu->BlendBright : 16);
        for (int i = 0; i < 4; i++)
        {
            rgb[0][i] += (((0x3F - rgb[0][i]) * bright) + 8) / 16;
        }
        break;
    }
    case BLDCR_Dark:
    {
        u8 bright = ((ppu->BlendBright <= 16) ? ppu->BlendBright : 16);
        for (int i = 0; i < 4; i++)
        {
            rgb[0][i] -= ((rgb[0][i] * bright) + 7) / 16;
        }
        break;
    }
    }
    return rgb[0][0] | (rgb[0][1] << 6) | (rgb[0][2] << 12);
}

void PPU_Composite(struct Console* sys, const bool b, const u16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    volatile u16* palbase = (b ? &sys->Palette.b16[0x400/sizeof(u16)] : &sys->Palette.b16[0]);
    u16 (*BGExtPal)(struct Console*, const u16) = (b ? VRAM_BGBExtPal : VRAM_BGAExtPal);
    u16 (*OBJExtPal)(struct Console*, const u16) = (b ? VRAM_OBJBExtPal : VRAM_OBJAExtPal);
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? b : !b][y];
    volatile timestamp* time = (b ? (&sys->PPUBTimestamp) : (&sys->PPUATimestamp));

    bool winenable = (ppu->DisplayCR.Win0Enable || ppu->DisplayCR.Win1Enable || ppu->DisplayCR.OBJWinEnable);
    for (int x = 0; x < 256; x++)
    {
        // checkme: idk when this is actually checked
        // TODO: DSi with PPU rev enabled special cases the ((left == 0) && (right == 0)) case
        if (x == ppu->Window.W0Right) ppu->Window0XActive = false;
        else if (x == ppu->Window.W0Left) ppu->Window0XActive = true;

        if (x == ppu->Window.W1Right) ppu->Window1XActive = false;
        else if (x == ppu->Window.W1Left) ppu->Window1XActive = true;

        // calc window
        WindowCR enablemask;
        if (ppu->DisplayCR.Win0Enable && ppu->Window0YActive && ppu->Window0XActive)
        {
            enablemask = ppu->Window.W0Cr;
        }
        else if (ppu->DisplayCR.Win1Enable && ppu->Window1YActive && ppu->Window1XActive)
        {
            enablemask = ppu->Window.W1Cr;
        }
        else if (ppu->DisplayCR.OBJWinEnable && (ppu->SpriteWindow[x/64] & ((u64)1<<(x%64))))
        {
            enablemask = ppu->Window.WObjCr;
        }
        else if (winenable)
        {
            enablemask = ppu->Window.WNoneCr;
        }
        else
        {
            enablemask.Raw = 0x3F;
        }

        int bg[2];
        // initialize with bg color
        CompositeBuffer index[2] = {(CompositeBuffer){0, 0, false, false, false /* checkme? */, false, false},
                                    (CompositeBuffer){0, 0, false, false, false /* checkme? */, false, false}};
        int i = 0;
        for (int prio = 0; prio < 4; prio++)
        {
            // check if sprites should be rendered
            CompositeBuffer tmp = ppu->CompositeBuffer[4][x];
            if (!tmp.Empty && (tmp.SprPrio == prio) && enablemask.EnableObj)
            {
                bg[i] = 4;
                index[i] = tmp;
                i++;
                if (i == 2) goto exit;
            }

            // check bgs
            for (int g = 0; g < 4; g++)
            {
                // check window
                if (!(enablemask.Raw & (1<<g))) continue;

                // check priority
                if (ppu->BGCR[g].BGPriority != prio) continue;

                // check if bg exists
                tmp = ppu->CompositeBuffer[g][x];
                if (!tmp.Empty)
                {
                    bg[i] = g;
                    index[i] = tmp;
                    i++;
                    if (i == 2) goto exit;
                }
            }
        }
        // no bg selected
        bg[i] = 5;
        i++;

        exit:

        u32 color[2];
        for (int j = 0; j < i; j++)
        {
            if (index[j].NotPal) color[j] = index[j].Index;
            else
            {
                if (index[j].ExtPal)
                {
                    if (bg[j] == 4)
                    {
                        color[j] = OBJExtPal(sys, index[j].Index);
                    }
                    else
                    {
                        u32 extpalbase = bg[j]*(KiB(8)/sizeof(u16));
                        if ((bg[j] <= 1) && ppu->BGCR[bg[j]].ExtPalSlot) extpalbase += KiB(16)/sizeof(u16);
                        color[j] = BGExtPal(sys, index[j].Index + extpalbase);
                    }
                }
                else
                {
                    AddBusContention(sys->AHBBusyTS, *time, Dev_Palette);
                    color[j] = palbase[(index[j].Index & 0xFF) + ((bg[j] == 4) ? 256 : 0)];
                }
                color[j] = RGB565to666(color[j]);
            }

            *time += (i == 2) ? 3 : 6;
            PPU_Wait(sys, *time);
        }

        scanline[x] = PPU_Blend(ppu, index, color, bg, i, enablemask.EnableBlend);
    }
    *time += 2+HBlank_Cycles;
}

void PPU_SpriteAffine(struct Console* sys, const bool b, const SprAttrs01 attr1, const SprAttrs2 attr2, const u8 width, const u8 height, u8 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[4];
    u32 (*OBJ)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_OBJB : VRAM_OBJA);

    // fetch rotation and scaling parameters
    volatile u32* oambase = (b ? &sys->OAM.b32[0x400/sizeof(u32)] : &sys->OAM.b32[0]);
    volatile u32* rotscalbase = &oambase[attr1.AffineParam*(32/sizeof(typeof(*oambase)))];
    // rot/scal params are stored in the gaps between each sprite's oam data.
    s16 params[4] = {rotscalbase[1] >> 16, rotscalbase[3] >> 16, rotscalbase[5] >> 16, rotscalbase[7] >> 16};

    // todo: mosaic

    // double size sprites have their bounding boxes doubled
    u8 widthreal = width;
    u8 heightreal = height;
    if (attr1.DoubleSize)
    {
        widthreal*=2;
        heightreal*=2;
    }

    s16 x; // x coordinate relative to screen
    s16 sx; // x coordinate relative to left edge of sprite's bounding box
    if (attr1.X < 0)
    {
        // if x is negative, start rendering from the left edge of the screen. (checkme: do oob pixels matter?)
        x = 0;
        sx = 0-attr1.X;
    }
    else
    {
        // otherwise start rendering from left edge of the sprite.
        x = attr1.X;
        sx = 0;
    }

    // MATRIX MATH WOOOOOOOOOOOOOOOOOOOOO
    // calculate rotscal thingies
    // sprites are rotated around their center
    u32 rotx = ((((s32)sx-(widthreal/2)) * params[0]) + (((s32)y-(heightreal/2)) * params[1]) + (width*256/2));
    u32 roty = ((((s32)sx-(widthreal/2)) * params[2]) + (((s32)y-(heightreal/2)) * params[3]) + (height*256/2));

    if (attr1.Mode == 3) // bitmap sprite
    {
        u8 alpha = attr2.BitmapAlpha;
        if (!alpha) return; // checkme?
        // increase by one to allow for max alpha value
        // shift left by one to convert to 5 bit alpha (for blending)
        alpha = (alpha + 1) << 1;

        u32 baseaddr;
        u32 ystep;
        if (ppu->DisplayCR.BitmapOBJ1D)
        {
            if (ppu->DisplayCR.BitmapOBJ2DDims)
            {
                // apparently does nothing??
                return; // checkme
            }
            else
            {
                baseaddr = attr2.TileNum << (7+ppu->DisplayCR.BitmapOBJ1DBound);
                ystep = width;
            }
        }
        else
        {
            if (ppu->DisplayCR.BitmapOBJ2DDims)
            {
                baseaddr = ((attr2.TileNum % 32) * 16) + ((attr2.TileNum / 32) * 32 * 4);
                ystep = 256;
            }
            else
            {
                baseaddr = ((attr2.TileNum % 16) * 16) + ((attr2.TileNum / 16) * 16 * 8);
                ystep = 128;
            }
        }

        for (; (sx < widthreal) && (x < 256); x++, sx++, rotx+=params[0], roty+=params[2])
        {
            if ((rotx < (width*256)) && (roty < (height*256)))
            {
                u32 addr = baseaddr + (((roty / 256) * ystep) + ((rotx / 256)) * 2);
                u16 color = OBJ(sys, addr & ~3, 0xFFFFFFFF, false, 0, false) >> ((addr & 0x2) * 8);

                if ((color & 0x8000) && (buffer[x].Empty || (buffer[x].SprPrio > attr2.Priority)))
                    buffer[x] = (CompositeBuffer){RGB565to666(color & 0x7FFF) | (alpha<<18), attr2.Priority, false, true, false, true, true};
            }
        }
    }
    else // not bitmap
    {
        u32 baseaddr = attr2.TileNum;
        u32 ystep;

        if (ppu->DisplayCR.TileOBJ1D)
        {
            baseaddr <<= ppu->DisplayCR.TileOBJ1DBound;
            ystep = (width/8) << attr1.Pal256;
        }
        else
        {
            ystep = 0x20;
        }

        baseaddr *= 32;
        ystep *= 32;

        for (; (sx < widthreal) && (x < 256); sx++, x++, rotx+=params[0], roty+=params[2])
        {
            if (rotx < (width*256) && roty < (height*256))
            {
                u32 xtile = (rotx>>11) * 64;
                u32 ytile = (roty>>11) * ystep;
                u32 xpixel = (rotx>>8) % 8;
                u32 ypixel = ((roty>>8) % 8) * 8;
                u32 xpixfrac = xpixel & 1;

                if (!attr1.Pal256)
                {
                    xtile/=2;
                    xpixel/=2;
                    ypixel/=2;
                }

                u32 addr = baseaddr + ytile + ypixel + xtile + xpixel;
                u16 index = OBJ(sys, addr & ~3, 0xFFFFFFFF, false, 0, false) >> ((addr & 0x3) * 8);

                if (attr1.Pal256)
                {
                    index &= 0xFF;
                    if (!index) continue;
                    index |= attr2.PaletteOffset * 256;
                }
                else
                {
                    index = (index >> (xpixfrac * 4)) & 0xF;
                    if (!index) continue;
                    index |= attr2.PaletteOffset * 16;
                }

                if (attr1.Mode == 2) // window
                    ppu->SpriteWindow[x/64] |= (u64)1<<(x%64);
                else if (buffer[x].Empty || (buffer[x].SprPrio > attr2.Priority))
                    buffer[x] = (CompositeBuffer){index, attr2.Priority, false, false, attr1.Pal256 && ppu->DisplayCR.SprExtPalEn, attr1.Mode == 1, false};
            }
        }
    }
}

void PPU_SpriteNormal(struct Console* sys, const bool b, const SprAttrs01 attr1, const SprAttrs2 attr2, const u8 width, const u8 height, u8 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[4];
    u32 (*OBJ)(struct Console*, const u32, const u32, const bool, const u32, const bool) = (b ? VRAM_OBJB : VRAM_OBJA);

    // vertical flip flag means we start from the bottom of the sprite
    if (attr1.VFlip) y = ((height-1) - y);

    s16 x; // x coordinate relative to screen
    s16 sx; // x coordinate relative to left edge of sprite's bounding box
    if (attr1.X < 0)
    {
        // if x is negative, start rendering from the left edge of the screen. (checkme: do oob pixels matter?)
        x = 0;
        sx = 0-attr1.X;
    }
    else
    {
        // otherwise start rendering from left edge of the sprite.
        x = attr1.X;
        sx = 0;
    }

    if (attr1.Mode == 3) // bitmap
    {
        u8 alpha = attr2.BitmapAlpha;
        if (!alpha) return; // checkme?
        // increase by one to allow for max alpha value
        // shift left by one to convert to 5 bit alpha (for blending)
        alpha = (alpha + 1) << 1;

        u32 baseaddr = attr2.TileNum;
        if (ppu->DisplayCR.BitmapOBJ1D)
        {
            if (ppu->DisplayCR.BitmapOBJ2DDims)
            {
                // apparently does nothing??
                return; // checkme
            }
            else
            {
                baseaddr = attr2.TileNum << (7+ppu->DisplayCR.BitmapOBJ1DBound);
                baseaddr += y * (width * 2);
            }
        }
        else
        {
            if (ppu->DisplayCR.BitmapOBJ2DDims)
            {
                baseaddr = ((attr2.TileNum % 32) * 16) + ((attr2.TileNum / 32) * 32 * 128);
                baseaddr += y * (256*2);
            }
            else
            {
                baseaddr = ((attr2.TileNum % 16) * 16) + ((attr2.TileNum / 16) * 16 * 128);
                baseaddr += y* (128*2);
            }
        }

        if (attr1.HFlip) sx = (width-1) - sx;

        for (; ((attr1.HFlip) ? (sx >= 0) : (sx < width)) && (x < 256); ((attr1.HFlip) ? (sx-=1) : (sx+=1)), x++)
        {
            u32 addr = baseaddr + (sx*2);

            u16 color = OBJ(sys, addr&~3, 0xFFFFFFFF, false, 0, false) >> ((addr&2)*8);

            if ((color & 0x8000) && (buffer[x].Empty || (buffer[x].SprPrio > attr2.Priority)))
                buffer[x] = (CompositeBuffer){RGB565to666(color & 0x7FFF) | (alpha<<18), attr2.Priority, false, true, false, true, true};
        }
    }
    else // not bitmap
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

        // add y pixel offset
        baseaddr = (baseaddr*32) + ((y%8) << (2+attr1.Pal256));

        if (attr1.HFlip) sx = (width-1) - sx;

        for (; ((attr1.HFlip) ? (sx >= 0) : (sx < width)) && (x < 256); ((attr1.HFlip) ? (sx-=1) : (sx+=1)), x++)
        {
            u32 addr = baseaddr + ((sx/8*8) << (2+attr1.Pal256)) + ((sx%8) >> !attr1.Pal256);
            u16 index = OBJ(sys, addr&~3, 0xFFFFFFFF, false, 0, false) >> ((addr&3)*8);

            if (attr1.Pal256)
            {
                index &= 0xFF;
                if (!index) continue;
                index |= attr2.PaletteOffset * 256; // for extpal
            }
            else
            {
                index = (index >> ((sx&1) * 4)) & 0xF;
                if (!index) continue;
                index |= attr2.PaletteOffset * 16;
            }

            if (attr1.Mode == 2) // window
                ppu->SpriteWindow[x/64] |= (u64)1<<(x%64);
            else if (buffer[x].Empty || (buffer[x].SprPrio > attr2.Priority))
                buffer[x] = (CompositeBuffer){index, attr2.Priority, false, false, attr1.Pal256 && ppu->DisplayCR.SprExtPalEn, attr1.Mode == 1, false};
        }
    }
}

void PPU_BuildSprites(struct Console* sys, const bool b, const u8 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    // checkme: does this need to be volatile to handle thread sync properly?
    volatile u32* oambase = (b ? &sys->OAM.b32[0x400/sizeof(u32)] : &sys->OAM.b32[0]);

    PPU_None(sys, b, 4); // clear sprite buffer
    memset(ppu->SpriteWindow, 0, sizeof(ppu->SpriteWindow)); // clear sprite window bitfield

    // sprites can be disabled entirely in the ppu's control reg
    if (!ppu->DisplayCR.SprEnable) return;

    // note: sprites are rendered from lowest slot to highest slot
    // this does mean that their oam slot priority resolves as a result of each sprite overwriting the other
    // but it does mean we end up calculating a lot of pixels that dont end up getting used
    // but doing it the way hardware does it will allow for implementing sprite timings
    for (int spr = 0; spr < 128; spr++)
    {
        SprAttrs01 attr = {.Raw = oambase[spr*2]};

        // check if sprite is disabled
        if (!attr.Affine && attr.Disable) continue;

        // calc sprite bounds
        u8 width = 8 << attr.Size;
        u8 height = 8 << attr.Size;

        if (attr.Shape == 1)
        {
            // horizontal: height is cut in half
            height /= 2;
            if (height <= 8)
            {
                // if the height ends up <= 8 px it's clamped to 8 and the width is doubled
                height = 8;
                width *= 2;
            }
        }
        else if (attr.Shape == 2)
        {
            // vertical: width is cut in half
            width /= 2;
            if (width <= 8)
            {
                // if the width ends up <= 8 px it's clamped to 8 and the height is doubled
                width = 8;
                height *= 2;
            }
        }
        else if (attr.Shape == 3)
        {
            // invalid shape: supposedly just results in it being 8x8 everytime?
            // checkme?
            width = 8;
            height = 8;
        }

        // get the y progress through the sprite
        // note: sprites can wrap the screen if they overflow the 8 bit range (checkme?)
        u8 sy = (y - attr.Y) & 0xFF;
        // check if we're actually within the sprite's bounding box
        // note: affine sprites support doubling the size of their bounding boxes
        if (sy >= (height << (attr.Affine && attr.DoubleSize))) continue;

        // checkme: does it skip sprites based on x coordinate?

        // fetch attribute 2 now that we know this sprite will render
        SprAttrs2 attr2 = {.Raw = (oambase[(spr*2)+1] & 0xFFFF)};

        if (attr.Affine)
        {
            PPU_SpriteAffine(sys, b, attr, attr2, width, height, sy);
        }
        else
        {
            PPU_SpriteNormal(sys, b, attr, attr2, width, height, sy);
        }
    }
}

void ApplyBrightnessModifier(u32* scanline, Brightness bright)
{
    if (bright.Mode != 1 && bright.Mode != 2) return; // checkme: mode 3?
    for (int x = 0; x < 256; x++)
    {
        u32 out = 0;
        u8 factor = ((bright.Factor > 16) ? 16 : bright.Factor);
        for (int i = 0; i < 18; i += 6)
        {
            s16 c = (scanline[x] >> i) & 0x3F;
            if (bright.Mode == 1)
            {
                c += ((0x3F - c) * factor) >> 4;
            }
            else
            {
                c -= ((c * factor) + 0xF) >> 4;
            }
            out |= c << i;
        }
        scanline[x] = out;
    }
}

void PPU_RenderScanline(struct Console* sys, const bool b, const s16 y)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    volatile timestamp* time = (b ? (&sys->PPUBTimestamp) : (&sys->PPUATimestamp));

    if (y >= 0)
    {
        u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? b : !b][y];
        switch(ppu->DisplayCR.DisplayMode)
        {
            case 0:
                *time += 1538 + HBlank_Cycles;
                for (int x = 0; x < 256; x++)
                    scanline[x] = 0x3FFFF;
                ApplyBrightnessModifier(scanline, ppu->Brightness);
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
                ApplyBrightnessModifier(scanline, ppu->Brightness);
                return;
            }
            case 3:
                LogPrint(LOG_PPU|LOG_UNIMP, "INVALID LCDC MODE 3\n");
                *time += 1538 + HBlank_Cycles;
                ApplyBrightnessModifier(scanline, ppu->Brightness);
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
        ApplyBrightnessModifier(scanline, ppu->Brightness);
    }
    if (y < 191)
    {
        if (y == -1)
            *time += 1538 + HBlank_Cycles;
        PPU_BuildSprites(sys, b, y+1);
    }
}

void PPU_GlobalStep(struct Console* sys, const timestamp now, const u16 vcount)
{
    PPU* ppus[2] = {&sys->PPU_A, &sys->PPU_B};

    PPU_Sync(sys, now);

    // windowing y coord is checked every scanline, even ones that aren't rendered on
    for (int i = 0; i < 2; i++)
    {
        // BUG: the NDS only tests the bottom 8 bits of the y coordinate.
        // TODO: disable this masking when DSi ppu revision bit is enabled.
        u16 wy = vcount & 0xFF;

        if (ppus[i]->Window.W0Bot == wy) ppus[i]->Window0YActive = false;
        else if (ppus[i]->Window.W0Top == wy) ppus[i]->Window0YActive = true;

        if (ppus[i]->Window.W1Bot == wy) ppus[i]->Window1YActive = false;
        else if (ppus[i]->Window.W1Top == wy) ppus[i]->Window1YActive = true;
    }
}

int SDLCALL PPUA_MainLoop(void* ptr)
{
    struct Console* sys = ptr;
    while (!sys->PPUStart) SDL_CPUPauseInstruction();

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

int SDLCALL PPUB_MainLoop(void* ptr)
{
    struct Console* sys = ptr;
    while (!sys->PPUStart) SDL_CPUPauseInstruction();

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
