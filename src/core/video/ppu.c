#include <stdckdint.h>
#include "core/console.h"
#include "core/scheduler.h"
#include "core/bus/vram.h"
#include "video.h"
#include "ppu.h"



typedef u16 (*BGVRAMPTR)(Console*, const u32);

BGVRAMPTR PPU_GetBGVRAM(const PPU_Type type)
{
    switch(type)
    {
        case PPUType_A: return VRAM_BGA;
        case PPUType_B: return VRAM_BGB;
        case PPUType_AGB: return VRAM_BGAGB;
    }
}
typedef u16 (*OBJVRAMPTR)(Console*, const u32);

OBJVRAMPTR PPU_GetOBJVRAM(const PPU_Type type)
{
    switch(type)
    {
        case PPUType_A: return VRAM_OBJA;
        case PPUType_B: return VRAM_OBJB;
        case PPUType_AGB: return VRAM_OBJAGB;
    }
}

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

u16 PPU_VRAMLookup(Console* sys, const u32 addr, u16 (*func)(Console*, const u32))
{
    const struct
    {
        u16* bank;
        size_t size;
    } vram[VRAMID_MAX] =
    {
        {sys->VRAM_A.b16, VRAM_A_Size},
        {sys->VRAM_B.b16, VRAM_B_Size},
        {sys->VRAM_C.b16, VRAM_C_Size},
        {sys->VRAM_D.b16, VRAM_D_Size},
        {sys->VRAM_E.b16, VRAM_E_Size},
        {sys->VRAM_F.b16, VRAM_F_Size},
        {sys->VRAM_G.b16, VRAM_G_Size},
        {sys->VRAM_H.b16, VRAM_H_Size},
        {sys->VRAM_I.b16, VRAM_I_Size}
    };

    u16 list = func(sys, addr);

    if (!list)
    {
        return 0;
    }
    else if (stdc_count_ones(list) == 1)
    {
        u8 id = stdc_trailing_zeros(list);
        u16* bank = vram[id].bank;
        size_t size = vram[id].size;
        return bank[(addr & (size-1))/2];
    }
    else
    {
        // todo: contention handling...?
        u16 rdata = 0;
        while (list)
        {
            u8 id = stdc_trailing_zeros(list);
            u16* bank = vram[id].bank;
            size_t size = vram[id].size;
            rdata |= bank[(addr & (size-1))/2];
            list &= (~1)<<id;
        }
        return rdata;
    }
}

void PPU_StepNone(Console* sys, const u16 h, const u8 bg, const PPU_Type type)
{
    // checkme: do the bgs get ignored during compositing instead of returning null?
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];

    s32 x = h;
    if (x < (PPU_IterLen*8)) return;
    x -= (PPU_IterLen*8);
    if ((x % PPU_IterLen) != bg) return; // checkme?
    x /= PPU_IterLen;

    if (x >= LCD_Width) return;

    buffer[x] = (CompositeBuffer){0, 0, true, false, false, false, false};
}

void PPU_StepTextBG(Console* sys, const u16 h, const u16 v, const u8 bg, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];

    s32 cycle = h - (((BGTileWidth-1) - (ppu->Xoff[bg] % BGTileWidth)) * PPU_IterLen);

    if ((cycle % PPU_IterLen) != bg) return; // not this bg's turn
    if (cycle < 0) return; // rendering has not started for this bg.

    s32 tilereq = (LCD_Width / BGTileWidth) + ((ppu->Xoff[bg] % BGTileWidth) != 0);
    s32 cyclepertile = (PPU_IterLen * BGTileWidth);
    s32 cyclelimit = tilereq*cyclepertile + PPU_IterLen;
    if (cycle > cyclelimit) return; // we have rendered enough tiles.

    u16 y = v + ppu->Yoff[bg];

    // determine which step this is
    s32 step = ((cycle / PPU_IterLen)-1) % BGTileWidth;

    if ((cycle / PPU_IterLen) == 0) ppu->BGDat[bg].BgX = (ppu->Xoff[bg] & ~(BGTileWidth-1));

    u16 xbg = ppu->BGDat[bg].BgX;
    u8 xscr = ((h / PPU_IterLen) - 8);

    xbg %= (BGScreenWidthPx << ppu->BGCR[bg].Wide);
    y %= (BGScreenHeightPx << ppu->BGCR[bg].Tall);
    if (((cycle / PPU_IterLen) == 0) || ((step == 0) && ((cycle / PPU_IterLen) != 1)))
    {
        // look up next tile
        u32 screenbase = ppu->BGCR[bg].ScreenBase * KiB(2);
        if (type == PPUType_A) // ntr mode ppu a only
            screenbase += ppu->DisplayCR.ScreenBase * KiB(64);

        screenbase += ((y % (BGScreenWidthPx << ppu->BGCR[bg].Tall)) / BGTileWidth) * BGScreenWidthBytes;
        // if it is also wide then we have to offset the height some to get the proper screen offset if it's past the 256th pixel.
        if (ppu->BGCR[bg].Tall && ppu->BGCR[bg].Wide)
            screenbase += ((y & ~(BGScreenWidthPx-1)) / BGTileWidth) * BGScreenWidthBytes;

        u32 tileaddr = screenbase + (((xbg % BGScreenWidthPx) / BGTileWidth) * BGBytesPerTile);
        if (ppu->BGCR[bg].Wide) tileaddr += ((xbg & ~(BGScreenWidthPx-1)) * BGTileWidth);
        ppu->BGDat[bg].Tile.Raw = PPU_VRAMLookup(sys, tileaddr, PPU_GetBGVRAM(type));
    }
    if ((cycle / PPU_IterLen) != 0)
    {
        TextTileData* tile = &ppu->BGDat[bg].Tile;

        u32 tilebase = ppu->BGCR[bg].CharBase * KiB(16);
        if (type == PPUType_A) // ntr mode ppu a only
            tilebase += ppu->DisplayCR.CharBase * KiB(64);

        u8 xfrac = xbg % BGTileWidth;
        if (tile->HFlip) xfrac = ((BGTileWidth-1) - xfrac);

        if ((step % (ppu->BGCR[bg].Pal256 ? 2 : 4)) == 0)
        {
            // fetch new palette data
            u8 yfrac = y % BGTileHeight;
            if (tile->VFlip) yfrac = ((BGTileHeight-1) - yfrac);

            u32 pixeladdr = (tile->TileNum * BGTileSize) + (yfrac * BGTileWidth) + xfrac;
            if (!ppu->BGCR[bg].Pal256) // 4bpp; 1 byte contains two entries
                pixeladdr /= 2;

            ppu->BGDat[bg].ColorData = PPU_VRAMLookup(sys, tilebase+pixeladdr, PPU_GetBGVRAM(type));
        }

        u8 bpp = (ppu->BGCR[bg].Pal256 ? 8 : 4);
        u8 idx = ppu->BGDat[bg].ColorData >> ((xfrac * bpp) % 16) & ((1<<bpp)-1);

        ppu->BGDat[bg].BgX++;

        if (xscr < 0 || xscr >= LCD_Width) return; // oops nothing needs to render here!

        buffer[xscr] = (CompositeBuffer){idx+(tile->Palette*(1<<bpp)), 0, !idx, false, ppu->BGCR[bg].Pal256&&ppu->DisplayCR.BGExtPalEn, false, false};
    }
}

void PPU_StepAffineX(PPU* ppu, const u8 bg)
{
    ppu->AffineCur[bg-2][0] += ppu->AffineRotParams[bg-2][0];
    ppu->AffineCur[bg-2][1] += ppu->AffineRotParams[bg-2][2];
}

void PPU_StepAffineY(PPU* ppu, const u8 bg)
{
    ppu->AffineStart[bg-2][0] += ppu->AffineRotParams[bg-2][1];
    ppu->AffineStart[bg-2][1] += ppu->AffineRotParams[bg-2][3];
}

void PPU_ReloadAffineCoords(PPU* ppu, const u16 h, const u16 v, const u8 bg, const PPU_Type type)
{
    if (h == PPU_IterLen)
    {
        PPU_StepAffineY(ppu, bg);
        if ((v == 0) || ppu->AffineDirty[bg-2])
        {
            ppu->AffineStart[bg-2][0] = ppu->AffineCoord[bg-2][0];
            ppu->AffineStart[bg-2][1] = ppu->AffineCoord[bg-2][1];
            ppu->AffineDirty[bg-2] = false;
        }
        ppu->AffineCur[bg-2][0] = ppu->AffineStart[bg-2][0];
        ppu->AffineCur[bg-2][1] = ppu->AffineStart[bg-2][1];
    }
}

void PPU_StepAffineTextBG(Console* sys, const u16 h, const u16 v, const bool ext, const u8 bg, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];

    s32 cycle = h;
    s32 step;
    s32 xscr;

    // checkme: when is this done?
    PPU_ReloadAffineCoords(ppu, h, v, bg, type);

    if (type == PPUType_AGB)
    {
        if (cycle < (8*PPU_IterLen)) return; // not time yet
        cycle -= (8*PPU_IterLen);
        if (((cycle % PPU_IterLen) / 2) != (1-(bg-2))) return; // bg3 runs on cycles 0-1; bg2 runs on cycles 2-3
        step = cycle % 2;
        xscr = cycle / PPU_IterLen;
    }
    else // ntr
    {
        if (cycle < (8*PPU_IterLen)) return; // not time yet
        cycle -= (8*PPU_IterLen);
        if (((cycle % PPU_IterLen) / 2) != (bg-1)) return; // bg2 runs on cycles 2-3; bg3 runs on cycles 4-5
        step = cycle % 2;
        xscr = cycle / PPU_IterLen;
    }

    u32 x = (u32)(ppu->AffineCur[bg-2][0] >> 8);
    u32 y = (u32)(ppu->AffineCur[bg-2][1] >> 8);

    u32 screensize;
    screensize = (128 << (ppu->BGCR[bg].ScreenSize));
    if (ppu->BGCR[bg].OverflowWrap)
    {
        x %= screensize;
        y %= screensize;
    }

    if (step == 0)
    {
        // look up next tile
        u32 screenbase = ppu->BGCR[bg].ScreenBase * KiB(2);
        if (type == PPUType_A) // ntr mode ppu a only
            screenbase += ppu->DisplayCR.ScreenBase * KiB(64);

        screenbase += (((y % screensize) / BGTileWidth) * (screensize/BGTileWidth)) << ext;

        u32 tileaddr = screenbase + (((x % screensize) / BGTileWidth) << ext);
        ppu->BGDat[bg].Tile.Raw = PPU_VRAMLookup(sys, tileaddr, PPU_GetBGVRAM(type));
    }
    if (step == 1)
    {
        TextTileData* tile = &ppu->BGDat[bg].Tile;

        if (!ext)
        {
            if (x & 0x8) tile->Raw >>= 8; // checkme?
            tile->Raw &= 0xFF;
        }

        u32 tilebase = ppu->BGCR[bg].CharBase * KiB(16);
        if (type == PPUType_A) // ntr mode ppu a only
            tilebase += ppu->DisplayCR.CharBase * KiB(64);

        u8 xfrac = x % BGTileWidth;
        if (tile->HFlip) xfrac = ((BGTileWidth-1) - xfrac);

        // fetch new palette data
        u8 yfrac = y % BGTileHeight;
        if (tile->VFlip) yfrac = ((BGTileHeight-1) - yfrac);

        u32 pixeladdr = (tile->TileNum * BGTileSize) + (yfrac * BGTileWidth) + xfrac;

        ppu->BGDat[bg].ColorData = PPU_VRAMLookup(sys, tilebase+pixeladdr, PPU_GetBGVRAM(type));

        u8 bpp = 8;
        u8 idx = ppu->BGDat[bg].ColorData >> ((xfrac * bpp) % 16) & ((1<<bpp)-1);

        if (xscr < LCD_Width) // oops nothing needs to render here!
        {
            bool empty = ((x >= screensize) || (y >= screensize)) || (idx == 0);
            buffer[xscr] = (CompositeBuffer){idx+(tile->Palette*(1<<bpp)), 0, empty, false, ext&&ppu->DisplayCR.BGExtPalEn, false, false};
        }

        PPU_StepAffineX(ppu, bg); // checkme: does it still keep stepping affine coords?
    }
}

void PPU_StepAffineBitmapBG(Console* sys, const u16 h, const u16 v, const bool dircolor, const u8 bg, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[bg];

    s32 cycle = h;
    u32 screenbase;
    u32 width;
    u32 height;

    // checkme: when is this done?
    PPU_ReloadAffineCoords(ppu, h, v, bg, type);
    u16 xscr;

    if (type == PPUType_AGB)
    {
        if (cycle < (8*PPU_IterLen)) return; // not time yet
        cycle -= (8*PPU_IterLen);
        if ((cycle % PPU_IterLen) != 3) return; // wait for the proper cycle
        xscr = cycle /= PPU_IterLen;

        screenbase = ((ppu->DisplayCR.AGB.FrameSelect && (ppu->DisplayCR.BGMode != 3)) ? 0xA000 : 0);
        switch(ppu->DisplayCR.BGMode)
        {
            case 3: width = 240; height = 160; break;
            case 4: width = 240; height = 160; break;
            case 5: width = 160; height = 128; break;
        }
    }
    else // nds
    {
        if (cycle < (8*PPU_IterLen)) return; // not time yet
        cycle -= (8*PPU_IterLen);
        if ((cycle % PPU_IterLen) != ((bg == 2) ? 2 : 4)) return; // wait for the proper cycle. CHECKME: what are the proper cycles for nds mode?
        xscr = cycle /= PPU_IterLen;

        screenbase = ppu->BGCR[bg].ScreenBase * KiB(16);
        switch(ppu->BGCR[bg].ScreenSize)
        {
            case 0: width = 128; height = 128; break;
            case 1: width = 256; height = 256; break;
            case 2: width = 512; height = 256; break;
            case 3: width = 512; height = 512; break;
        }
    }

    u32 x = (u32)(ppu->AffineCur[bg-2][0] >> 8);
    u32 y = (u32)(ppu->AffineCur[bg-2][1] >> 8);

    if ((type != PPUType_AGB) && ppu->BGCR[bg].OverflowWrap)
    {
        x %= width;
        y %= height;
    }

    if (dircolor)
    {
        u32 addr = screenbase + (x*2) + (y*(width*2));
        u16 color = PPU_VRAMLookup(sys, addr, PPU_GetBGVRAM(type));

        if (xscr < LCD_Width)
        {
            bool empty = ((x >= width) || (y >= height)) || ((type == PPUType_AGB) ? false : !(color & 0x8000));
            buffer[xscr] = (CompositeBuffer){RGB565to666(color&0x7FFF), 0, empty, true, false, false, false};
        }
    }
    else // 8 bit index
    {
        u32 addr = screenbase + x + (y*width);
        u8 idx = PPU_VRAMLookup(sys, addr, PPU_GetBGVRAM(type)) >> ((addr&0x1)*8);

        if (xscr < LCD_Width)
        {
            bool empty = ((x >= width) || (y >= height)) || (idx == 0);
            buffer[xscr] = (CompositeBuffer){idx, 0, empty, false, false, false, false};
        }
    }
    PPU_StepAffineX(ppu, bg);
}

void PPU_StepExtAffineBG(Console* sys, const u16 h, const u16 v, const u8 bg, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);

    if (ppu->BGCR[bg].Pal256)
    {
        PPU_StepAffineBitmapBG(sys, h, v, ppu->BGCR[bg].CharBase & 1, bg, type);
    }
    else
    {
        return PPU_StepAffineTextBG(sys, h, v, true, bg, type);
    }
}

void PPU_3D(Console* sys, const u16 h, const u16 v)
{
    CompositeBuffer* buffer = sys->PPU_A.CompositeBuffer[0];
    if (h < (PPU_GetIterCycles(false)*8)) return; // not yet
    u16 x = h - (PPU_GetIterCycles(false)*8);
    if ((x % PPU_GetIterCycles(false)) != 0) return; // bg0 cycle
    x /= PPU_GetIterCycles(false);
    if (x >= LCD_GetWidth(false)) return;

    SWRen_SyncRenderedLines(sys, v+1);
    buffer[x] = (CompositeBuffer){sys->GX3D.CBuf[0][v][x] + (1<<18) /* increase alpha to allow for max alpha for blending */, 0, ((sys->GX3D.CBuf[0][v][x] >> 18) & 0x1F) == 0, true, false, true, true};
}

void AGBPPU_BG0Mode(Console* sys, const u16 h, const u16 v)
{
    PPU* ppu = &sys->PPU_A;
    if (!ppu->DisplayCR.BG0Enable) return PPU_StepNone(sys, h, 0, PPUType_AGB);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0 ... 1:
            return PPU_StepTextBG(sys, h, v, 0, PPUType_AGB);

        default:
            return PPU_StepNone(sys, h, 0, PPUType_AGB);
    }
}

void AGBPPU_BG1Mode(Console* sys, const u16 h, const u16 v)
{
    PPU* ppu = &sys->PPU_A;
    if (!ppu->DisplayCR.BG1Enable) return PPU_StepNone(sys, h, 1, PPUType_AGB);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0 ... 1:
            return PPU_StepTextBG(sys, h, v, 1, PPUType_AGB);

        default:
            return PPU_StepNone(sys, h, 1, PPUType_AGB);

    }
}

void AGBPPU_BG2Mode(Console* sys, const u16 h, const u16 v)
{
    PPU* ppu = &sys->PPU_A;
    if (!ppu->DisplayCR.BG2Enable) return PPU_StepNone(sys, h, 2, PPUType_AGB);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0:
            return PPU_StepTextBG(sys, h, v, 2, PPUType_AGB);

        case 1 ... 2:
            return PPU_StepAffineTextBG(sys, h, v, false, 2, PPUType_AGB);

        case 3:
            return PPU_StepAffineBitmapBG(sys, h, v, true, 2, PPUType_AGB);
        case 4:
            return PPU_StepAffineBitmapBG(sys, h, v, false, 2, PPUType_AGB);
        case 5:
            return PPU_StepAffineBitmapBG(sys, h, v, true, 2, PPUType_AGB);

        case 6 ... 7:
            return PPU_StepNone(sys, h, 2, PPUType_AGB);

    }
}

void AGBPPU_BG3Mode(Console* sys, const u16 h, const u16 v)
{
    PPU* ppu = &sys->PPU_A;
    if (!ppu->DisplayCR.BG3Enable) return PPU_StepNone(sys, h, 3, PPUType_AGB);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0:
            return PPU_StepTextBG(sys, h, v, 3, PPUType_AGB);

        case 2:
            return PPU_StepAffineTextBG(sys, h, v, false, 3, PPUType_AGB);

        case 1:
        case 3 ... 7:
            return PPU_StepNone(sys, h, 3, PPUType_AGB);
    }
}

void NTRPPU_BG0Mode(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG0Enable) return PPU_StepNone(sys, h, 0, type);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0 ... 5:
            if (ppu->DisplayCR.BG03D && type == PPUType_A)
            {
                PPU_3D(sys, h, v);
                break;
            }
            [[fallthrough]];
        case 7:
            return PPU_StepTextBG(sys, h, v, 0, type);
            break;

        case 6:
            if (type == PPUType_A) PPU_3D(sys, h, v);
            else PPU_StepNone(sys, h, 0, type); // wtf happens to engine b?
            break;
    }
}

void NTRPPU_BG1Mode(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG1Enable) return PPU_StepNone(sys, h, 1, type);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0 ... 5:
        case 7:
            return PPU_StepTextBG(sys, h, v, 1, type);

        case 6:
            return PPU_StepNone(sys, h, 1, type);

    }
}

void NTRPPU_BG2Mode(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG2Enable) return PPU_StepNone(sys, h, 2, type);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0 ... 1:
        case 3:
            return PPU_StepTextBG(sys, h, v, 2, type);

        case 2:
        case 4:
            return PPU_StepAffineTextBG(sys, h, v, false, 2, type);

        case 5:
            return PPU_StepExtAffineBG(sys, h, v, 2, type);

        case 6:
            printf("LORGE\n"); break;
            //return PPU_Large(sys, b, y, 2);

        case 7:
            return PPU_StepNone(sys, h, 2, type);

    }
}

void NTRPPU_BG3Mode(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    if (!ppu->DisplayCR.BG3Enable) return PPU_StepNone(sys, h, 3, type);
    switch(ppu->DisplayCR.BGMode)
    {
        case 0:
            return PPU_StepTextBG(sys, h, v, 3, type);

        case 1 ... 2:
            return PPU_StepAffineTextBG(sys, h, v, false, 3, type);

        case 3 ... 5:
            return PPU_StepExtAffineBG(sys, h, v, 3, type);

        case 6 ... 7:
            return PPU_StepNone(sys, h, 3, type);
    }
}

void PPU_StepBGs(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    if (type == PPUType_AGB)
    {
        AGBPPU_BG0Mode(sys, h, v);
        AGBPPU_BG1Mode(sys, h, v);
        AGBPPU_BG2Mode(sys, h, v);
        AGBPPU_BG3Mode(sys, h, v);
    }
    else
    {
        NTRPPU_BG0Mode(sys, h, v, type);
        NTRPPU_BG1Mode(sys, h, v, type);
        NTRPPU_BG2Mode(sys, h, v, type);
        NTRPPU_BG3Mode(sys, h, v, type);
    }
}

void PPU_SpriteAffine(Console* sys, u8 sx, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[4+ppu->SprBufBack];

    SprData* dat = &ppu->SprDat[ppu->SprDatCur];
    const SprAttrs01 attr1 = dat->OAM01;
    const SprAttrs2 attr2 = dat->OAM2;
    const u8 width = dat->Width;
    const u8 height = dat->Height;
    s16* params = dat->AffineParams;

    // todo: mosaic

    u32 scrx = attr1.X + sx;

    u32 rotx = dat->AffineCur[0];
    u32 roty = dat->AffineCur[1];
    if (attr1.Mode == 3) // bitmap sprite
    {
        if (sys->AGB) return LogPrint(LOG_PPU|LOG_UNIMP, "BITMAP AFFINE OBJ ON GBA\n");
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

        if (((u32)rotx < (width*256)) && ((u32)roty < (height*256)) && (scrx < LCD_Width))
        {
            u32 addr = baseaddr + (((roty / 256) * ystep) + ((rotx / 256)) * 2);
            u16 color = PPU_VRAMLookup(sys, addr, PPU_GetOBJVRAM(type));

            if ((color & 0x8000) && (buffer[scrx].Empty || (buffer[scrx].SprPrio > attr2.Priority)))
                buffer[scrx] = (CompositeBuffer){RGB565to666(color & 0x7FFF) | (alpha<<18), attr2.Priority, false, true, false, true, true};
        }
    }
    else // not bitmap
    {
        u32 baseaddr = attr2.TileNum;
        u32 ystep;
        if (sys->AGB ? ppu->DisplayCR.AGB.OBJ1D : ppu->DisplayCR.TileOBJ1D)
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

        if ((rotx < (width*256)) && (roty < (height*256)) && (scrx < LCD_Width))
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
            u16 index = PPU_VRAMLookup(sys, addr, PPU_GetOBJVRAM(type)) >> ((addr&0x1)*8);

            if (attr1.Pal256)
            {
                index &= 0xFF;
                if (!index) goto inc;
                index |= attr2.PaletteOffset * 256;
            }
            else
            {
                index = (index >> (xpixfrac * 4)) & 0xF;
                if (!index) goto inc;
                index |= attr2.PaletteOffset * 16;
            }

            if (attr1.Mode == 2) // window
                ppu->SpriteWindow[ppu->SprBufBack][scrx/64] |= (u64)1<<(scrx%64);
            else if (buffer[scrx].Empty || (!sys->AGB && (buffer[scrx].SprPrio > attr2.Priority)))
                buffer[scrx] = (CompositeBuffer){index, attr2.Priority, false, false, attr1.Pal256 && ppu->DisplayCR.SprExtPalEn, attr1.Mode == 1, false};
        }
    }
    inc:
    dat->AffineCur[0] += params[0];
    dat->AffineCur[1] += params[2];
}

void PPU_SpriteNormal(Console* sys, u8 sx, u8 sy, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    CompositeBuffer* buffer = ppu->CompositeBuffer[4+ppu->SprBufBack];

    SprData* dat = &ppu->SprDat[ppu->SprDatCur];
    const SprAttrs01 attr1 = dat->OAM01;
    const SprAttrs2 attr2 = dat->OAM2;
    const u8 width = dat->Width;
    const u8 height = dat->Height;

    // vertical flip flag means we start from the bottom of the sprite
    if (attr1.VFlip) sy = ((height-1) - sy);

    u32 scrx = attr1.X + sx;

    if (attr1.Mode == 3) // bitmap
    {
        if (sys->AGB) return LogPrint(LOG_PPU|LOG_UNIMP, "BITMAP OBJ ON GBA\n");
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
                baseaddr += sy * (width * 2);
            }
        }
        else
        {
            if (ppu->DisplayCR.BitmapOBJ2DDims)
            {
                baseaddr = ((attr2.TileNum % 32) * 16) + ((attr2.TileNum / 32) * 32 * 128);
                baseaddr += sy * (256*2);
            }
            else
            {
                baseaddr = ((attr2.TileNum % 16) * 16) + ((attr2.TileNum / 16) * 16 * 128);
                baseaddr += sy * (128*2);
            }
        }

        if (attr1.HFlip) sx = (width-1) - sx;

        u32 addr = baseaddr + (sx*2);

        u16 color = PPU_VRAMLookup(sys, addr, PPU_GetOBJVRAM(type));


        if (scrx < LCD_Width)
        {
            if ((color & 0x8000) && (buffer[scrx].Empty || (buffer[scrx].SprPrio > attr2.Priority)))
                buffer[scrx] = (CompositeBuffer){RGB565to666(color & 0x7FFF) | (alpha<<18), attr2.Priority, false, true, false, true, true};
        }
    }
    else // not bitmap
    {
        u32 baseaddr = attr2.TileNum;
        if (sys->AGB ? ppu->DisplayCR.AGB.OBJ1D : ppu->DisplayCR.TileOBJ1D)
        {
            baseaddr <<= ppu->DisplayCR.TileOBJ1DBound;
            baseaddr += ((sy/8) * (width/8)) << attr1.Pal256;
        }
        else
        {
            baseaddr += (sy/8)*32;
        }

        // add y pixel offset
        baseaddr = (baseaddr*32) + ((sy%8) << (2+attr1.Pal256));

        if (attr1.HFlip) sx = (width-1) - sx;

        u32 addr = baseaddr + ((sx/8*8) << (2+attr1.Pal256)) + ((sx%8) >> !attr1.Pal256);

        if ((sx % 2) == attr1.HFlip) dat->ColorData = PPU_VRAMLookup(sys, addr, PPU_GetOBJVRAM(type));
        u16 index = dat->ColorData;

        if (attr1.Pal256)
        {
            index = (index >> ((sx&1) * 8)) & 0xFF;
            if (!index) return;
            index |= attr2.PaletteOffset * 256; // for extpal
        }
        else
        {
            index = (index >> ((sx&3) * 4)) & 0xF;
            if (!index) return;
            index |= attr2.PaletteOffset * 16;
        }

        if (scrx < LCD_Width)
        {
            if (attr1.Mode == 2) // window
                ppu->SpriteWindow[ppu->SprBufBack][scrx/64] |= (u64)1<<(scrx%64);
            else if (buffer[scrx].Empty || (!sys->AGB && (buffer[scrx].SprPrio > attr2.Priority)))
                buffer[scrx] = (CompositeBuffer){index, attr2.Priority, false, false, attr1.Pal256 && ppu->DisplayCR.SprExtPalEn, attr1.Mode == 1, false};
        }
    }
}

void PPU_SpriteFetchOAM(SprData* dat, volatile u32* oambase, const u16 y)
{
    if (dat->SprID > 127) return; // we all outta sprites
    if (dat->Cycle == 0)
    {
        // fetch oam 0 & 1
        SprAttrs01 attr = {.Raw = oambase[dat->SprID*2]};

        // check if sprite is disabled
        if (!attr.Affine && attr.Disable)
        {
            dat->SprID++;
            return;
        }

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
        if (sy >= (height << (attr.Affine && attr.DoubleSize)))
        {
            dat->SprID++;
            return;
        }

        dat->Cycle++;
        dat->OAM01.Raw = oambase[dat->SprID*2];
        dat->Height = height;
        dat->Width = width;
    }
    else if (dat->Cycle == 2)
    {
        // fetch oam 2
        dat->OAM2.Raw = oambase[(dat->SprID*2)+1];
        dat->Cycle++;
    }
    else dat->Cycle++;
}

void PPU_StepSprites(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    // checkme: does this need to be volatile to handle thread sync properly?
    volatile u32* oambase = ((type == PPUType_B) ? &sys->OAM.b32[0x400/sizeof(u32)] : &sys->OAM.b32[0]);

    if (h == (10*PPU_IterLen)) // swap sprite buffer
    {
        ppu->SprBufBack = !ppu->SprBufBack;
        ppu->SprDat[0] = (SprData){};
        ppu->SprDatCur = 0;

        for (int x = 0; x < 256; x++) // clear back sprite buffer
            ppu->CompositeBuffer[4+ppu->SprBufBack][x] = (CompositeBuffer){0, 0, true, false, false, false, false};

        memset(ppu->SpriteWindow[ppu->SprBufBack], 0, sizeof(ppu->SpriteWindow[ppu->SprBufBack])); // clear back sprite window bitfield
    }

    // sprites can be disabled entirely in the ppu's control reg
    if (!ppu->DisplayCR.SprEnable) return;

    if (((type == PPUType_AGB) ? ppu->DisplayCR.AGB.OBJHBlankDisable : ppu->DisplayCR.OBJHBlankDisable) // do not render sprites during hblank
     && ((h < (10*PPU_IterLen)) || (h >= LCD_HBlankStart))) // sprite hblank range
        return;

    u32 y = ((v == LCD_VMax) ? 0 : (v+1));

    // note: sprites are rendered from lowest slot to highest slot
    // this does mean that their oam slot priority resolves as a result of each sprite overwriting the other
    // but it does mean we end up calculating a lot of pixels that dont end up getting used
    // but doing it the way hardware does it will allow for implementing sprite timings
    SprData* dat = &ppu->SprDat[ppu->SprDatCur];

    //if (dat->SprID < 128) printf("%i %i\n", dat->SprID, dat->Cycle);

    if (dat->Cycle <= 2) PPU_SpriteFetchOAM(dat, oambase, y);
    else if (dat->Cycle == 3)
    {
        ppu->SprDat[!ppu->SprDatCur] = (SprData){.SprID = dat->SprID+1};
        dat->Cycle++;
    }
    else if (dat->OAM01.Affine && (dat->Cycle <= 15)) // fetch rotscale
    {
        // rot/scal params are stored in the gaps between each sprite's oam data.
        volatile u32* rotscalbase = &oambase[dat->OAM01.AffineParam*(32/4)];
        switch(dat->Cycle)
        {
            case 0 ... 2: unreachable();
            case 4: dat->AffineParams[0] = rotscalbase[1] >> 16; dat->Cycle++; break;
            case 8: dat->AffineParams[1] = rotscalbase[3] >> 16; dat->Cycle++; break;
            case 10: dat->AffineParams[2] = rotscalbase[5] >> 16; dat->Cycle++; break;
            case 12: dat->AffineParams[3] = rotscalbase[7] >> 16; dat->Cycle++; break;
            case 14:
            {
                // get the y progress through the sprite
                // note: sprites can wrap the screen if they overflow the 8 bit range (checkme?)
                u8 sy = (y - dat->OAM01.Y) & 0xFF;
                // double size sprites have their bounding boxes doubled
                u8 widthreal = dat->Width;
                u8 heightreal = dat->Height;
                if (dat->OAM01.DoubleSize)
                {
                    widthreal*=2;
                    heightreal*=2;
                }
                s16* params = dat->AffineParams;

                // MATRIX MATH WOOOOOOOOOOOOOOOOOOOOO
                // calculate rotscal thingies
                // sprites are rotated around their center
                dat->AffineCur[0] = ((((s32)0-(widthreal/2)) * params[0]) + (((s32)sy-(heightreal/2)) * params[1]) + (dat->Width*256/2));
                dat->AffineCur[1] = ((((s32)0-(widthreal/2)) * params[2]) + (((s32)sy-(heightreal/2)) * params[3]) + (dat->Height*256/2));
                [[fallthrough]];
            }
            case 15: PPU_SpriteFetchOAM(&ppu->SprDat[!ppu->SprDatCur], oambase, y); dat->Cycle++; break; // run next sprites's first two cycles
            default: dat->Cycle++; break;
        }
    }
    else // fetch obj vram
    {
        u32 cydelay = (dat->OAM01.Affine ? 16 : 4);
        s32 max = dat->Width - 1;
        s32 xoffs = (dat->Cycle - cydelay);

        if (dat->Cycle <= 5)
        {
            PPU_SpriteFetchOAM(&ppu->SprDat[!ppu->SprDatCur], oambase, y); // run next sprites's first two cycles
        }

        if (dat->OAM01.Affine)
        {
            max *= 2;
            if (dat->OAM01.DoubleSize) max *= 2;
            if ((dat->Cycle % 2) == 1) // speculation: affine bgs need two cycles due to calc'ing affine params
            {
                dat->Cycle++;
                return;
            }
        }

        // get the y progress through the sprite
        // note: sprites can wrap the screen if they overflow the 8 bit range (checkme?)
        u8 sy = (y - dat->OAM01.Y) & 0xFF;
        if (dat->OAM01.Affine) PPU_SpriteAffine(sys, xoffs/2, type);
        else                   PPU_SpriteNormal(sys, xoffs, sy, type);

        dat->Cycle++;

        if (xoffs == (max))//-1))
        {
            PPU_SpriteFetchOAM(&ppu->SprDat[!ppu->SprDatCur], oambase, y); // run next sprites's third cycle
            ppu->SprDatCur = !ppu->SprDatCur;
        }
    }
}

u32 PPU_Blend(PPU* ppu)
{
    u8 alpA;
    u8 alpB;

    u16 rgb[2][4] = {{ppu->CompCol[0] & 0x3F, (ppu->CompCol[0] >> 6) & 0x3F, (ppu->CompCol[0] >> 12) & 0x3F, 0},
                     {ppu->CompCol[1] & 0x3F, (ppu->CompCol[1] >> 6) & 0x3F, (ppu->CompCol[1] >> 12) & 0x3F, 0}};

    // override normal logic to always alpha blend
    if ((ppu->CompBGs[1] >= 0) && ppu->CompSel[0].ForceBlend && (ppu->BlendCR.BlendBot & (1<<ppu->CompBGs[1])))
    {
        if (ppu->CompSel[0].HasAlpha)
        {
            alpA = (ppu->CompCol[0] >> 18);

            alpB = 32 - alpA;
            goto Alpha_Blend;
        }
        else goto Force_Blend;
    }

    if (!ppu->WinEnMask.EnableBlend || !(ppu->BlendCR.BlendTop & (1<<ppu->CompBGs[0])))
        return ppu->CompCol[0];

    switch(ppu->BlendCR.Effect)
    {
    case BLDCR_Off:
        return ppu->CompCol[0];

    case BLDCR_Blend:
        // there must be a lower pixel and it must be blendable
        if ((ppu->CompBGs[1] < 0) || !(ppu->BlendCR.BlendBot & (1<<ppu->CompBGs[1])))
            return ppu->CompCol[0];

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

void PPU_FetchPalette(Console* sys, const int num, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);
    volatile u16* palbase;
    u16 (*bgextpalvram)(Console*, const u32);
    u16 (*objextpalvram)(Console*, const u32);
    switch(type)
    {
    case PPUType_A:
        bgextpalvram = VRAM_BGAExtPal;
        objextpalvram = VRAM_OBJAExtPal;
        palbase = &sys->Palette.b16[0];
        break;
    case PPUType_B:
        bgextpalvram = VRAM_BGBExtPal;
        objextpalvram = VRAM_OBJBExtPal;
        palbase = &sys->Palette.b16[0x400/2];
        break;
    case PPUType_AGB:
        palbase = &sys->Palette.b16[0];
        break;
    }

    if (ppu->CompBGs[num] < 0) return; // -1 implies no bg, should only trigger for bottom pixel
    if ((num == 1) // bottom pixel is only fetched if blending is enabled.
        && !((ppu->CompSel[0].ForceBlend || ((ppu->BlendCR.BlendTop & (1<<ppu->CompBGs[0])) && (ppu->BlendCR.Effect == BLDCR_Blend) && ppu->WinEnMask.EnableBlend)) // test if alpha blending is active and top pixel allows it
        && (ppu->BlendCR.BlendBot & (1<<ppu->CompBGs[1])))) // test if bottom pixel can be blended
        return;

    if (ppu->CompSel[num].NotPal) ppu->CompCol[num] = ppu->CompSel[num].Index;
    else
    {
        if (ppu->CompSel[num].ExtPal) // extended palette lookup
        {
            if (type == PPUType_AGB) unreachable();
            if (ppu->CompBGs[num] == 4) // spr
            {
                ppu->CompCol[num] = PPU_VRAMLookup(sys, ppu->CompSel[num].Index*2, objextpalvram);
            }
            else // bg layers
            {
                u32 extpalbase = ppu->CompBGs[num] * KiB(8);
                if ((ppu->CompBGs[num] <= 1) && ppu->BGCR[ppu->CompBGs[num]].ExtPalSlot) extpalbase += KiB(16);
                ppu->CompCol[num] = PPU_VRAMLookup(sys, extpalbase + (ppu->CompSel[num].Index*2), bgextpalvram);
            }
        }
        else // palette ram
        {
            //AddBusContention(sys, *time, Dev_Palette);
            ppu->CompCol[num] = palbase[(ppu->CompSel[num].Index & 0xFF) + ((ppu->CompBGs[num] == 4 /* index sprite pram instead */) ? 256 : 0)];
        }
        ppu->CompCol[num] = RGB565to666(ppu->CompCol[num]);
    }
}

void PPU_StepComposite(Console* sys, const u16 h, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);

    s32 cycle = h - 1 - (11*PPU_IterLen);
    u16 x = cycle / PPU_IterLen;
    if (cycle < 0) return; // not ready
    if (cycle % PPU_IterLen == 0)
    {
        // sel pixels for blending
        // initialize with bg color

        if (x > LCD_Width) return; // nothing to do here

        ppu->CompBGs[0] = -1;
        ppu->CompBGs[1] = -1;
        ppu->CompSel[0] = (CompositeBuffer){0, 0, false, false, false /* checkme? */, false, false};
        ppu->CompSel[1] = (CompositeBuffer){0, 0, false, false, false /* checkme? */, false, false};
        int i = 0;
        for (int prio = 0; prio < 4; prio++)
        {
            // check if sprites should be rendered
            CompositeBuffer tmp = ppu->CompositeBuffer[4+!ppu->SprBufBack][x];
            if (!tmp.Empty && (tmp.SprPrio == prio) && ppu->WinEnMask.EnableObj)
            {
                ppu->CompBGs[i] = 4;
                ppu->CompSel[i] = tmp;
                i++;
                if (i == 2) goto exit;
            }

            // check bgs
            for (int g = 0; g < 4; g++)
            {
                // check window
                if (!(ppu->WinEnMask.Raw & (1<<g))) continue;

                // check priority
                if (ppu->BGCR[g].BGPriority != prio) continue;

                // check if bg exists
                tmp = ppu->CompositeBuffer[g][x];
                if (!tmp.Empty)
                {
                    ppu->CompBGs[i] = g;
                    ppu->CompSel[i] = tmp;
                    i++;
                    if (i == 2) goto exit;
                }
            }
        }
        // no bg selected; use backdrop
        ppu->CompBGs[i] = 5;

        exit:

        // fetch top pixel
        PPU_FetchPalette(sys, 0, type);
    }
    if (cycle % PPU_IterLen == 2)
    {
        if (x > LCD_Width) return; // nothing to do here

        // fetch bottom pixel
        PPU_FetchPalette(sys, 1, type);

        // finally output a pixel!!!
        ppu->OutLine[x] = PPU_Blend(ppu);
    }
}

void PPU_StepWindows(Console* sys, const u16 h, const u16 v, const PPU_Type type)
{
    PPU* ppu = ((type == PPUType_B) ? &sys->PPU_B : &sys->PPU_A);

    s32 cycle = h - 1 - (11*PPU_IterLen);
    u16 x = cycle / PPU_IterLen;
    if (cycle < 0) return; // not ready
    if (cycle % PPU_IterLen != 0) return;
    // check windows
    if (ppu->DisplayCR.Win0Enable || ppu->DisplayCR.Win1Enable || ppu->DisplayCR.OBJWinEnable)
    {
        // NOTE: windowing checks are even out of bounds
        if (cycle == 0) // CHECKME: is this checking done while windows are disabled?
        {
            // Y WINDOW CHECK:

            // BUG: the NDS only tests the bottom 8 bits of the y coordinate.
            // TODO: disable this masking when DSi ppu revision bit is enabled.
            u16 winy = v & 0xFF;

            if      (ppu->Window.W0Bot == winy) ppu->Window0YActive = false;
            else if (ppu->Window.W0Top == winy) ppu->Window0YActive = true;

            if      (ppu->Window.W1Bot == winy) ppu->Window1YActive = false;
            else if (ppu->Window.W1Top == winy) ppu->Window1YActive = true;
        }

        // X WINDOW CHECK:

        // checkme: idk when this is actually checked
        // TODO: DSi with PPU rev enabled special cases the ((left == 0) && (right == 0)) case
        if      (x == ppu->Window.W0Right) ppu->Window0XActive = false;
        else if (x == ppu->Window.W0Left ) ppu->Window0XActive = true;

        if      (x == ppu->Window.W1Right) ppu->Window1XActive = false;
        else if (x == ppu->Window.W1Left ) ppu->Window1XActive = true;

        // calc window
        if (ppu->DisplayCR.Win0Enable && ppu->Window0YActive && ppu->Window0XActive) // win 0 active
            ppu->WinEnMask = ppu->Window.W0Cr;
        else if (ppu->DisplayCR.Win1Enable && ppu->Window1YActive && ppu->Window1XActive) // win 1 active
            ppu->WinEnMask = ppu->Window.W1Cr;
        else if (ppu->DisplayCR.OBJWinEnable && (ppu->SpriteWindow[!ppu->SprBufBack][x/64] & ((u64)1<<(x%64)))) // win 2 active
            ppu->WinEnMask = ppu->Window.WObjCr;
        else // no window active
            ppu->WinEnMask = ppu->Window.WNoneCr;
    }
    else // windowing disabled
        ppu->WinEnMask.Raw = 0x3F;
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

void LCDC_Step(Console* sys, const bool b)
{
    PPU* ppu = (b ? &sys->PPU_B : &sys->PPU_A);
    // TODO: timings

    if (sys->LCDY[b] >= LCD_GetHeight(false)) return;
    u32* scanline = sys->Framebuffer[sys->BackBuf][sys->PowerCR9.AOnBottom ? b : !b][sys->LCDY[b]];
    if (sys->AGB && !b) goto agb; // TODO: should be vram display
    switch(ppu->DisplayCR.DisplayMode)
    {
        case 0: // nothing
            noblit:
            for (int x = 0; x < 256; x++)
                scanline[x] = 0x3FFFF;
            break;

        case 1: // ppu output
            agb:
            if (b ? !sys->PowerCR9.PPUBPower : !sys->PowerCR9.PPUAPower) goto noblit;
            //if (sys->VCount >= 192) goto noblit; // checkme: is this the proper way to implement this?

            if (sys->AGB) // hacky bullshit
            {
                if (sys->VCountAGB >= 160) goto noblit;
                for (int x = 0; x < 256; x++)
                {
                    if (x < 240) scanline[x] = ppu->OutLine[x];
                    else scanline[x] = 0x3FFFF;
                }
            }
            else
            {
                for (int x = 0; x < 256; x++)
                    scanline[x] = ppu->OutLine[x];
            }
            break;

        case 2: // vram display
        {
            if (b) unreachable();
            u32 addr = (ppu->DisplayCR.VRAMSel * KiB(128)) + (256*2*sys->LCDY[0]);
            for (int x = 0; x < 256; x++)
            {
                scanline[x] = RGB555to666(PPU_VRAMLookup(sys, addr, VRAM_LCD));
                addr += 2;
            }
            break;
        }
        case 3: // unused
            if (b) unreachable();
            LogPrint(LOG_PPU|LOG_UNIMP, "INVALID LCDC MODE 3\n");
            break;
    }
    ApplyBrightnessModifier(scanline, ppu->Brightness);
    sys->LCDY[b]++;
}

void PPU_Step(Console* sys, u16 h, u16 v, const PPU_Type type)
{
    if ((type == PPUType_B) ? !sys->PowerCR9.PPUBPower : !sys->PowerCR9.PPUAPower) return; // ppu off
    const u16 bgmaxv = LCD_GetHeight(type == PPUType_AGB);
    const u16 sprmaxv = LCD_GetHeight(type == PPUType_AGB) - 1;

    PPU_StepWindows(sys, h, v, type);
    if (v < bgmaxv && h < LCD_HBlankStart) // render bgs; composite layers
    {
        PPU_StepBGs(sys, h, v, type);
        PPU_StepComposite(sys, h, type);
    }

    if ((v == LCD_VMax) || (v < sprmaxv)) // render next sprite scanline
    {
        PPU_StepSprites(sys, h, v, type);
    }

    if (h == (2+((11+LCD_Width)*PPU_IterLen))) // end of render period
    {
        LCDC_Step(sys, type == PPUType_B);
    }
}

int SDLCALL PPUA_MainLoop(void* ptr)
{
    Console* sys = ptr;
    while (!sys->KillPPUs)
    {
        while (!sys->PPUStart) SDL_CPUPauseInstruction();

        if (sys->AGB)
        {
            if (sys->PPUATimestamp >= (sys->PPU_A.ScanlineStart + DSClk16(AGBScanline_Cycles)))
                sys->PPU_A.ScanlineStart = sys->PPUATimestamp;

            PPU_Wait(sys, sys->PPUATimestamp);

            PPU_Step(sys, (sys->PPUATimestamp-sys->PPU_A.ScanlineStart) / DSClk16(1), sys->VCountAGB, PPUType_AGB);
            sys->PPUATimestamp += DSClk16(1);
        }
        else
        {
            if (sys->PPUATimestamp >= (sys->PPU_A.ScanlineStart + DSClk33(NTRScanline_Cycles)))
                sys->PPU_A.ScanlineStart = sys->PPUATimestamp;

            PPU_Wait(sys, sys->PPUATimestamp);

            PPU_Step(sys, (sys->PPUATimestamp-sys->PPU_A.ScanlineStart) / DSClk33(1), sys->VCount, PPUType_A);
            sys->PPUATimestamp += DSClk33(1);
        }
    }
    return 0;
}

int SDLCALL PPUB_MainLoop(void* ptr)
{
    Console* sys = ptr;
    while (!sys->KillPPUs)
    {
        while (!sys->PPUStart) SDL_CPUPauseInstruction();

        if (sys->PPUBTimestamp >= (sys->PPU_B.ScanlineStart + DSClk33(NTRScanline_Cycles)))
            sys->PPU_B.ScanlineStart = sys->PPUBTimestamp;

        PPU_Wait(sys, sys->PPUBTimestamp);

        PPU_Step(sys, (sys->PPUBTimestamp-sys->PPU_B.ScanlineStart) / DSClk33(1), sys->VCount, PPUType_B);
        sys->PPUBTimestamp += DSClk33(1);
    }
    return 0;
}
