#include <stdlib.h>
#include "../console.h"
#include "3d.h"




u16 VRAM_3DTexel(struct Console* sys, u32 addr)
{
    addr &= (KiB(512)-1);
    u16 ret = 0;
    if ((sys->VRAMCR[0].Raw & 0x87) == 0x83)
    {
        if ((sys->VRAMCR[0].Offset * KiB(128)) == (addr & 0x60000))
            ret = sys->VRAM_A.b16[(addr & (VRAM_A_Size-1))/sizeof(u16)];
    }
    if ((sys->VRAMCR[1].Raw & 0x87) == 0x83)
    {
        if ((sys->VRAMCR[1].Offset * KiB(128)) == (addr & 0x60000))
            ret |= sys->VRAM_B.b16[(addr & (VRAM_B_Size-1))/sizeof(u16)];
    }
    if ((sys->VRAMCR[2].Raw & 0x87) == 0x83)
    {
        if ((sys->VRAMCR[2].Offset * KiB(128)) == (addr & 0x60000))
            ret |= sys->VRAM_C.b16[(addr & (VRAM_C_Size-1))/sizeof(u16)];
    }
    if ((sys->VRAMCR[3].Raw & 0x87) == 0x83)
    {
        if ((sys->VRAMCR[3].Offset * KiB(128)) == (addr & 0x60000))
            ret |= sys->VRAM_D.b16[(addr & (VRAM_D_Size-1))/sizeof(u16)];
    }
    return ret;
}

u16 VRAM_3DPal(struct Console* sys, u32 addr)
{
    //addr &= (KiB(512)-1); does this have any wrapping?
    u16 ret = 0;
    bool any [[maybe_unused]] = false;
    if ((sys->VRAMCR[4].Raw & 0x87) == 0x83)
    {
        if (!(addr & ~(KiB(64)-1)))
        {
            ret = sys->VRAM_E.b16[(addr & (VRAM_E_Size-1))/sizeof(u16)];
            any = true;
        }
    }
    if ((sys->VRAMCR[5].Raw & 0x87) == 0x83)
    {
        u32 base = ((sys->VRAMCR[5].Offset & 1) * KiB(16)) + ((sys->VRAMCR[5].Offset >> 1) * KiB(64));
        u32 index = (addr & ~(KiB(16)-1));

        if (base == index)
        {
            ret |= sys->VRAM_F.b16[(addr & (VRAM_F_Size-1))/sizeof(u16)];
            any = true;
        }
    }
    if ((sys->VRAMCR[6].Raw & 0x87) == 0x83)
    {
        u32 base = ((sys->VRAMCR[6].Offset & 1) * KiB(16)) + ((sys->VRAMCR[6].Offset >> 1) * KiB(64));
        u32 index = (addr & ~(KiB(16)-1));

        if (base == index)
        {
            ret |= sys->VRAM_G.b16[(addr & (VRAM_G_Size-1))/sizeof(u16)];
            any = true;
        }
    }
    return ret;
}

s32 SWRen_CalcSlope(u16 x0, u16 x1, u8 y0, u8 y1, u8 y, s16* xstart, s16* xend, s16* aacov, s16* aainc, const bool right)
{
    s16 xlen = x1 - x0;
    u8 ylen = (y1 - y0) & 0xFF; // this can overflow under very specific circumstances.

    s32 slope;
    if (ylen == 0) // note: this looks correct, but probably isn't actually correct.
    {
        slope = (1<<18) * xlen;
    }
    else if (ylen == xlen) // note: diagonal slopes need special handling
    {
        slope = (1<<18);
    }
    else if (ylen == -xlen)
    {
        slope = -(1<<18);
    }
    else
    {
        slope = ((1<<18) / ylen) * xlen;
    }

    bool xmajor = (abs(slope) > (1<<18));

    u8 ydiff = (y - y0) & 0xFF; // this can also overflow.

    // multiply slope by distance down the slope.
    s32 xoffs = ydiff * slope;

    // certain slopes need to be adjustments:

    // right slopes need to be shifted left one pixel
    if (right) xoffs -= (1<<18);
    // xmajor slopes round to the right by half a pixel
    if (xmajor) xoffs += (1<<18)/2;
    // negative xmajor slopes are shifted left one pixel
    if (slope < -(1<<18)) xoffs -= (1<<18);

    // truncate upper bits (s29)
    xoffs = (xoffs << 3) >> 3;

    // round towards zero
    if (slope < 0) xoffs += (1<<18)-1;

    // calculate span bounds:
    // note: the start and end coords of the span having different rounding during calc can result in single pixel gaps in the slope.

    // start coordinate is offset by the whole number component of the offset
    *xstart = x0 + (xoffs >> 18);
    if (xmajor)
        *xend = x0 + (((xoffs >> 9) + (slope >> 9)) >> 9);
    else // ymajor slopes are always 1 wide
        *xend = *xstart + ((slope < 0) ? -1 : 1);

    // negative slopes need to have their start and end swapped.
    if (slope < 0) DS_SWAP(*xstart, *xend);

#if 0
    /*
    step = width * (1<<10) / height;
    adj1 = step - (dx >> 8);
    adj2 = adj1 & (~step & 1);

    disp = (y - y0) * dx;
    fxs = neg ? x0frac - disp : x0frac + disp;
    fxs = (neg ? ((1<<18) - fsx - 1) : fsx) % (1<<18);
    basecov = (fxs >> 8) & ~1;
    bias = step / 2;

    if (basecov + step - adj1 >= (1<<10))
    {
        clamp max min?
    }
    out = base + bias - adj2;

    inv = side == (neg || xmajor);
    */
    // calculate aa coverage
    if ((ylen == 0) || (xlen == 0))
    {
        *aainc = 0;
        if ((ylen == 0) && (xlen == 0)) *aacov = 0;
        else *aacov = (1<<10)-1;
    }
    else
    {
        if (xmajor)
        {
            *aainc = ((ylen * (1<<10)) / (xlen));
            *aainc = (1<<10) - *aainc;
        }
        else
        {
            *aainc = 0;
            s32 step = ((abs(xlen) * (1<<10)) / ylen);
            s32 adj1 = step - (abs(slope) >> 8);
            s32 adj2 = adj1 & (~step & 1);

            s32 fracsx = ((x0 << 18) + (ydiff * slope));
            fracsx = ((fracsx < 0) ? ((1<<18) - fracsx - 1) : (fracsx)) % (1<<18);
            s32 basecov = (fracsx >> 8) & ~1;
            s32 bias = step / 2;

            if (basecov + step - adj1 >= (1<<10))
            {
                *aacov = ((1<<10)-1);
            }
            else
            {
                *aacov = (basecov + bias - adj2);
            }
        }
    }
    // certain slopes have inverted coverage
    if (right == xmajor)
    {
        *aainc ^= (1<<10)-1;
        *aacov ^= (1<<10)-1;
    }
#else
    *aacov = (1<<10)-1;
    *aainc = 0;
#endif

    return slope;
}

s32 SWRen_FindSlope(Polygon* poly, u8 y, s16* xstart, s16* xend, u8* vcur, u8* vnex, s16* aacov, s16* aainc, const bool right)
{
    *vcur = (*vnex = poly->VTop);
    do
    {
        *vcur = *vnex;
        if (poly->Frontfacing ^ right)
        {
            *vnex = (*vcur + 1) % poly->NumVert;
        }
        else
        {
            *vnex = *vcur - 1;
            if (*vnex >= poly->NumVert) *vnex = poly->NumVert-1;
        }
    }
    while ((y >= poly->SlopeY[*vnex]) && (*vcur != poly->VBot));

    return SWRen_CalcSlope(poly->Vertices[*vcur]->X, poly->Vertices[*vnex]->X, poly->Vertices[*vcur]->Y, poly->Vertices[*vnex]->Y, y, xstart, xend, aacov, aainc, right);
}

s32 SWRen_PerspectiveInterp(s16 x, const s16 xdiff, const u32 w0, const u32 w1, s32 a0, s32 a1, const bool yaxis)
{
    u32 inc; // note: this should probably be reused.
    u32 w0n, w0d, w1d;
    u8 shift;
    if (yaxis)
    {
        shift = 9;
        w0n = w0 >> 1;
        w0d = (w0 + (~w1 & 1)) >> 1;
        w1d = w1 >> 1;
    }
    else
    {
        shift = 8;
        w0n = w0;
        w0d = w0;
        w1d = w1;
    }
    u32 num = (x * w0n) << shift;
    u32 den = ((x * w0d) + ((xdiff-x) * w1d));

    if (den == 0) inc = 0;
    else inc = num / den;

    if (a0 < a1)
        return a0 + (((a1-a0) * inc) >> shift);
    else
        return a1 + (((a0-a1) * ((1<<shift)-inc)) >> shift);
}

bool SWRen_CheckPerspectiveLerp(const s32 w0, const s32 w1, const bool yaxis)
{
    u8 mask = yaxis ? 0x7E : 0x7F; // yaxis lerp discards lsb so we dont check it
    return !((w0 == w1) && !(w0 & mask) && !(w1 & mask));
}

s32 SWRen_Interpolate(s16 x, const s16 x0, const s16 x1, const u32 w0, const u32 w1, const s32 a0, const s32 a1, const bool yaxis, const bool persp, const bool borkedlerp)
{
    x -= x0;
    s16 xdiff = x1-x0;
    if ((x == 0) || (xdiff == 0)) return a0;
    if (x0 <= 0 && x1 > 511) return a1; // yes these values *can* occur. there is probably a better explanation for why this happens, but until we get a better answer we shall hardcode it in.

    if (persp)
    {
        // perspective correct interp using W values
        return SWRen_PerspectiveInterp(x, xdiff, w0, w1, a0, a1, yaxis);
    }
    else
    {
        if (borkedlerp)
        {
            // z interp seems to be buggy?
            // it loses a lot of precision based on how wide the polygon is...
            // this seems to be fairly close to what hardware actually does, as weird as it is.
            // this definitely isn't what its actually doing though...
            return a0 + ((a1-a0) / xdiff * x);
        }
        else
        {
            // this is not correct, it usually looks mostly correct, but it isn't.
            if (a0 < a1)
                return a0 + ((s64)(a1-a0) * x / xdiff);
            else
                return a1 + ((s64)(a0-a1) * (xdiff-x) / xdiff);
        }
    }
}

Colors SWRen_RGB555to666(Colors color)
{
    // the cast makes it faster i swear.
    color.RGB = (color.RGB << 1) - ((s32x4)color.RGB > 0);
    return color;
}

Colors SWRen_DecodeTextures(struct Console* sys, Polygon* poly, s16 s, s16 t, u8* texalpha)
{
    // discard fractional component
    s >>= 4;
    t >>= 4;

    u32 texbase = poly->TexAttr.Offset * 8;
    u32 paladdr = poly->TexPal;
    u16 slen = 8<<poly->TexAttr.SizeS;
    u16 tlen = 8<<poly->TexAttr.SizeT;

    s16* coord[2] = {&s, &t};
    u16 lens[2] = {slen, tlen};
    bool repeat[2] = {poly->TexAttr.RepeatS, poly->TexAttr.RepeatT};
    bool flip[2] = {poly->TexAttr.FlipS, poly->TexAttr.FlipT};
    // texcoord wrapping
    for (int i = 0; i < 2; i++)
    {
        if (repeat[i])
        {
            if (flip[i] && (*coord[i] & lens[i])) // handle flipping
            {
                *coord[i] = (lens[i]-1) - (*coord[i] & (lens[i]-1));
            }
            else *coord[i] &= lens[i]-1;
        }
        else // clamp coords
        {
            DS_CLAMP(*coord[i], <, 0);
            DS_CLAMP(*coord[i], >, lens[i]-1);
        }
    }
    u32 texoffs = (t*slen) + s;

    // decode texture based on type
    switch (poly->TexAttr.Format)
    {
    case 1: // A3I5
    {
        u8 texel = (VRAM_3DTexel(sys, texbase + texoffs) >> ((texoffs & 1) * 8)) & 0xFF;

        // isolate out alpha component
        *texalpha = (texel >> 5) & 0x7;
        // expand to 5 bit
        *texalpha = (*texalpha << 2) | (*texalpha >> 1);

        // isolate out texel data
        texel &= 0x1F;

        paladdr = (paladdr * 16) + (texel*2);
        u16 color = VRAM_3DPal(sys, paladdr);
        return SWRen_RGB555to666((Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F});
    }
    case 2: // 2I
    {
        u8 texel = (VRAM_3DTexel(sys, texbase + (texoffs/4)) >> ((texoffs & 0x7) * 2)) & 0x3;

        if ((texel == 0) && poly->TexAttr.Color0Trans)
        {
            *texalpha = 0;
            return (Colors){.R=0x3F, .G=0, .B=0x3F};
        }
        *texalpha = 31;

        paladdr = (paladdr * 8) + (texel*2);
        u16 color = VRAM_3DPal(sys, paladdr);
        return SWRen_RGB555to666((Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F});
    }
    case 3: // 4I
    {
        u8 texel = (VRAM_3DTexel(sys, texbase + (texoffs/2)) >> ((texoffs & 0x3) * 4)) & 0xF;

        if ((texel == 0) && poly->TexAttr.Color0Trans)
        {
            *texalpha = 0;
            return (Colors){.R=0x3F, .G=0, .B=0x3F};
        }
        *texalpha = 31;

        paladdr = (paladdr * 16) + (texel*2);
        u16 color = VRAM_3DPal(sys, paladdr);
        return SWRen_RGB555to666((Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F});
    }
    case 4: // 8I
    {
        u8 texel = (VRAM_3DTexel(sys, texbase + texoffs) >> ((texoffs & 1) * 8)) & 0xFF;

        if ((texel == 0) && poly->TexAttr.Color0Trans)
        {
            *texalpha = 0;
            return (Colors){.R=0x3F, .G=0, .B=0x3F};
        }
        *texalpha = 31;

        paladdr = (paladdr * 16) + (texel*2);
        u16 color = VRAM_3DPal(sys, paladdr);
        return SWRen_RGB555to666((Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F});
    }
    case 5: // 4x4 Compressed
    {
        //u32 addr = texbase + (texoffs/4);
        u32 addr = texbase + ((t & 0x3FC) * (slen / 4)) + (s & 0x3FC) + (t & 3);
        u8 texel;
        // slot 1 cannot be accessed for texel data here.
        if ((addr & 0x60000) == 0x20000) texel = 0;
        else texel = (VRAM_3DTexel(sys, addr) >> (((s & 0x3) + ((t & 0x1) << 2)) * 2)) & 0x3;

        // access slot one for palette index data
        u16 paldat = VRAM_3DTexel(sys, 0x20000 + ((addr & 0x1FFFF)/2) + ((addr >= 0x40000) ? 0x10000 : 0));
        paladdr = (paladdr * 16) + ((paldat & 0x3FFF) * 4);
        u8 mode = paldat >> 14;

        bool buggy;
        enum : u8
        {
            ColorNormal,
            ColorEven,
            Color0Bias,
            Color1Bias,
            ColorTrans,
        };

        switch (mode)
        {
            case 0:
            {
                mode = ((texel == 3) ? ColorTrans : ColorNormal);
                buggy = false;
                break;
            }

            case 1:
            {
                switch(texel)
                {
                    case 2: mode = ColorEven; break;
                    case 3: mode = ColorTrans; break;
                    default: mode = ColorNormal; break;
                }
                buggy = true; // should be disabled for dsi w/ rast rev bit set.
                break;
            }

            case 2:
            {
                mode = ColorNormal;
                buggy = false;
                break;
            }

            case 3:
            {
                switch(texel)
                {
                    case 2: mode = Color0Bias; break;
                    case 3: mode = Color1Bias; break;
                    default: mode = ColorNormal; break;
                }
                buggy = true; // should be disabled for dsi w/ rast rev bit set.
                break;
            }

            default: unreachable();
        }

        switch(mode)
        {
            case ColorNormal:
            {
                u16 color = VRAM_3DPal(sys, paladdr + (texel*2));
                *texalpha = 31;
                Colors colorfin = (Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F};
                if (buggy)
                {
                    colorfin.RGB <<= 1;
                }
                else
                {
                    colorfin = SWRen_RGB555to666(colorfin); 
                }
                return colorfin;
            }

            case ColorTrans:
            {
                *texalpha = 0;
                return (Colors){.R=0x3F, .G=0, .B=0x3F};
            }

            case ColorEven:
            {
                u16 color0 = VRAM_3DPal(sys, paladdr);
                u16 color1 = VRAM_3DPal(sys, paladdr+2);
                *texalpha = 31;
                Colors colorfin0 = (Colors){.R = color0 & 0x1F, .G = (color0 >> 5) & 0x1F, .B = (color0 >> 10) & 0x1F};
                Colors colorfin1 = (Colors){.R = color1 & 0x1F, .G = (color1 >> 5) & 0x1F, .B = (color1 >> 10) & 0x1F};
                colorfin0.RGB += colorfin1.RGB;
                return colorfin0;
            }

            case Color0Bias:
            {
                u16 color0 = VRAM_3DPal(sys, paladdr);
                u16 color1 = VRAM_3DPal(sys, paladdr+2);
                *texalpha = 31;
                Colors colorfin0 = (Colors){.R = color0 & 0x1F, .G = (color0 >> 5) & 0x1F, .B = (color0 >> 10) & 0x1F};
                Colors colorfin1 = (Colors){.R = color1 & 0x1F, .G = (color1 >> 5) & 0x1F, .B = (color1 >> 10) & 0x1F};
                colorfin0.RGB = ((colorfin0.RGB * 5) + (colorfin1.RGB * 3)) / 4;
                return colorfin0;
            }

            case Color1Bias:
            {
                u16 color0 = VRAM_3DPal(sys, paladdr);
                u16 color1 = VRAM_3DPal(sys, paladdr+2);
                *texalpha = 31;
                Colors colorfin0 = (Colors){.R = color0 & 0x1F, .G = (color0 >> 5) & 0x1F, .B = (color0 >> 10) & 0x1F};
                Colors colorfin1 = (Colors){.R = color1 & 0x1F, .G = (color1 >> 5) & 0x1F, .B = (color1 >> 10) & 0x1F};
                colorfin0.RGB = ((colorfin0.RGB * 3) + (colorfin1.RGB * 5)) / 4;
                return colorfin0;
            }

            default: unreachable();
        }
    }
    case 6: // A5I3
    {
        u8 texel = (VRAM_3DTexel(sys, texbase + texoffs) >> ((texoffs & 1) * 8)) & 0xFF;

        // isolate out alpha component
        *texalpha = (texel >> 3) & 0x1F;

        // isolate out texel data
        texel &= 0x7;

        paladdr = (paladdr * 16) + (texel*2);
        u16 color = VRAM_3DPal(sys, paladdr);
        return SWRen_RGB555to666((Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F});
    }
    case 7: // direct color
    {
        u16 color = VRAM_3DTexel(sys, texbase + (texoffs*2));
        *texalpha = ((color & 0x8000) ? 31 : 0);
        return SWRen_RGB555to666((Colors){.R = color & 0x1F, .G = (color >> 5) & 0x1F, .B = (color >> 10) & 0x1F});
    }
    default: unreachable();
    }
}

Colors SWRen_BlendColors(Polygon* poly, Colors color, Colors tcolor, u8 talpha, u8* outalpha)
{
    color.RGB >>= 3;
    Colors outcol;
    if (poly->Attrs.Mode & 1) // decal and shadow
    {
        // CHECKME: this division is weird, does hardware really do that?
        // (melonds has special case logic for talpha == 0 / 31, so i guess that's how hardware gets around the incorrect math?)
        outcol.RGB = ((tcolor.RGB * talpha) + (color.RGB * (31-talpha))) / 32;
        *outalpha = poly->Attrs.Alpha;
    }
    else // modulate / toon/highlight
    {
        outcol.RGB = (((tcolor.RGB+1) * (color.RGB+1)) - 1) / 64;
        *outalpha = ((talpha+1) * (poly->Attrs.Alpha+1) - 1) / 32;
    }

    // clamp to max
    //outcol.RGB = (outcol.RGB & ~((s32x4)outcol.RGB > 63)) | ((u32x4){63, 63, 63, 0} & ((s32x4)outcol.RGB > 63));
    //if (*outalpha > 31) *outalpha = 31;

    // wireframes ignore all alpha
    if (poly->Attrs.Alpha == 0) *outalpha = 31;

    return outcol;
}

void SWRen_AlphaBlend(GX3D* gx, const Polygon* poly, const u16 x, const u8 y, const s32 z, Colors fincolor, u8 finalpha, AttrBuf attr, const bool bot, const bool fill)
{
    const bool shadow = (poly->Attrs.PolyID != 0) && (poly->Attrs.Mode == 3);

    AttrBuf abuf = gx->ABuf[bot][y][x];

    attr.EdgeFlags = abuf.EdgeFlags; // checkme?
    attr.Backfacing = abuf.Backfacing; // checkme?
    attr.AACov = abuf.AACov; // checkme

    if ((abuf.Trans || shadow) && (attr.PolygonID == abuf.PolygonID))
        return;

    attr.Trans = true;

    u32 c = gx->CBuf[bot][y][x];
    Colors oldc = {.R = (c & 0x3F), .G = ((c >> 6) & 0x3F), .B = ((c >> 12) & 0x3F)};
    u8 olda = (c >> 18) & 0x1F;

    if (gx->LatRasterCR.AlphaBlend)
    {
        fincolor.RGB = ((fincolor.RGB * (finalpha+1)) + (oldc.RGB * (32-(finalpha+1)))) / 32;
    }
    else if (!fill) // fill applies if alpha blending is enabled
        return;

    // checkme: it just takes the highest alpha of the two?
    DS_CLAMP(finalpha, <, olda)

    gx->CBuf[bot][y][x] = fincolor.R | (fincolor.G << 6) | (fincolor.B << 12) | finalpha << 18;
    gx->ABuf[bot][y][x] = attr;
    if (poly->Attrs.TransDepthUpdate)
        gx->ZBuf[bot][y][x] = z;
}

bool SWRen_DepthTest(const GX3D* gx, const bool equaldt, const u16 x, const u8 y, s32 z, const AttrBuf attr, const bool bot)
{
    AttrBuf abuf = gx->ABuf[bot][y][x];
    s32 zbuf = gx->ZBuf[bot][y][x];

    if (equaldt)
    {
        // checkme: this is probably slightly wrong.
        u32 diff = abs((s32)(zbuf - z));
        return (diff <= 256);
    }
    else
    {
        // in certain cases the depth test is actually less than or equals
        if (((!attr.Backfacing && abuf.Backfacing) // frontfacing > backfacing
                && !(attr.BotXMajor && abuf.TopXMajor) // unless the edge flag in the buffer has priority
                && !(attr.LeftYMajor && abuf.RightYMajor))
            || (attr.TopXMajor && abuf.BotXMajor) // top xmajor > bot. xmajor
            || (attr.LeftYMajor && abuf.RightYMajor)) // left y major > right y major
            return (z <= zbuf);
        else
            return (z < zbuf);
    }
}

void SWRen_RasterizePixel(GX3D* gx, Polygon* poly, u16 x, u8 y, s32 z, Colors color, Colors tcolor, u8 talpha, AttrBuf attr, const bool fill)
{
    const bool stencil = (poly->Attrs.PolyID == 0) && (poly->Attrs.Mode == 3);
    const bool shadow = (poly->Attrs.PolyID != 0) && (poly->Attrs.Mode == 3);

    u8 finalpha;
    Colors fincolor = SWRen_BlendColors(poly, color, tcolor, talpha, &finalpha);

    if (finalpha <= gx->LatAlphaThreshold) return;

    if (finalpha < 31 || stencil || shadow)
    {
        attr.EdgeFlags = 0; // checkme?
    }

    bool bot = false;
    if (!SWRen_DepthTest(gx, poly->Attrs.EqualDepthTest, x, y, z, attr, false))
    {
        if (stencil) gx->SBuf[false][y%2][x] = true;
        bot = true;
        if (!SWRen_DepthTest(gx, poly->Attrs.EqualDepthTest, x, y, z, attr, true))
        {
            if (stencil) gx->SBuf[true][y%2][x] = true;
            return;
        }
    }
    if (stencil) return;

    if (shadow && !gx->SBuf[bot][y%2][x]) return;

    if ((finalpha == 31) && !shadow) // opaque
    {
        if (!fill) return;

        // move top pixel to bottom layer.
        gx->CBuf[true][y][x] = gx->CBuf[false][y][x];
        gx->ABuf[true][y][x] = gx->ABuf[false][y][x];
        gx->ZBuf[true][y][x] = gx->ZBuf[false][y][x];

        gx->CBuf[bot][y][x] = fincolor.R | (fincolor.G << 6) | (fincolor.B << 12) | finalpha << 18;
        gx->ABuf[bot][y][x] = attr;
        gx->ZBuf[bot][y][x] = z;
    }
    else // translucent & shadow(?)
    {
        if (!bot) SWRen_AlphaBlend(gx, poly, x, y, z, fincolor, finalpha, attr, false, fill);
        SWRen_AlphaBlend(gx, poly, x, y, z, fincolor, finalpha, attr, true, fill);
    }
}

void SWRen_RasterizePoly(struct Console* sys, Polygon* poly, const u8 y)
{
    GX3D* gx = &sys->GX3D;
    if ((y == poly->Bot) && (y != poly->Top)) return; // checkme: timings?

    const bool stencil = (poly->Attrs.PolyID == 0) && (poly->Attrs.Mode == 3);
    const bool shadow = (poly->Attrs.PolyID != 0) && (poly->Attrs.Mode == 3);

    if (stencil && gx->StencilClear[y%2])
    {
        gx->StencilClear[y%2] = false;
        memset(gx->SBuf[0][y%2], 0, 256);
        memset(gx->SBuf[1][y%2], 0, 256);
    }
    else if (shadow)
    {
        gx->StencilClear[y%2] = true;
    }

    s16 ls, le, rs, re;
    s32 lslope, rslope;
    u8 lc, ln, rc, rn;
    s16 lcov, lcovinc, rcov, rcovinc;
    lslope = SWRen_FindSlope(poly, y, &ls, &le, &lc, &ln, &lcov, &lcovinc, false);
    rslope = SWRen_FindSlope(poly, y, &rs, &re, &rc, &rn, &rcov, &rcovinc, true);

    if ((rslope == 0) && ((lslope != 0) || (ls != re)))
    {
        re--;
        rs--;
    }

    AttrBuf attr = (AttrBuf){.Backfacing = !poly->Frontfacing, .PolygonID = poly->Attrs.PolyID};

    if (ls > re)
    {
        DS_SWAP(ls, re)
        DS_SWAP(lslope, rslope) // checkme?
        attr.Backfacing = !attr.Backfacing;

        // checkme
        lcov = ~lcov;
        rcov = ~rcov;

        rs = re - 1;
        le = ls + 1;
        DS_SWAP(lc, rc)
        DS_SWAP(ln, rn)
    }

    u8 yc = poly->Vertices[lc]->Y;
    u8 yn = poly->Vertices[ln]->Y;
    u32 wc = poly->W[lc];
    u32 wn = poly->W[ln];
    s32 zc = (gx->LatWBuffer ? poly->W[lc] : poly->Vertices[lc]->Z) << poly->ZDecompress;
    s32 zn = (gx->LatWBuffer ? poly->W[ln] : poly->Vertices[ln]->Z) << poly->ZDecompress;
    u8 interpy = y + (lslope <= -(1<<18));
    bool persp = SWRen_CheckPerspectiveLerp(wc, wn, true);
    u32 wl = SWRen_Interpolate(interpy, yc, yn, wc, wn, wc, wn, true, persp, false);
    s32 zl = SWRen_Interpolate(interpy, yc, yn, wc, wn, zc, zn, true, gx->LatWBuffer, true);
    Colors cl;
    cl.R = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[lc]->Color.R, poly->Vertices[ln]->Color.R, true, persp, false);
    cl.G = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[lc]->Color.G, poly->Vertices[ln]->Color.G, true, persp, false);
    cl.B = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[lc]->Color.B, poly->Vertices[ln]->Color.B, true, persp, false);
    s32 sl = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[lc]->S+0, poly->Vertices[ln]->S+0, true, persp, false);
    s32 tl = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[lc]->T+0, poly->Vertices[ln]->T+0, true, persp, false);

    yc = poly->Vertices[rc]->Y;
    yn = poly->Vertices[rn]->Y;
    wc = poly->W[rc];
    wn = poly->W[rn];
    zc = (gx->LatWBuffer ? poly->W[rc] : poly->Vertices[rc]->Z) << poly->ZDecompress;
    zn = (gx->LatWBuffer ? poly->W[rn] : poly->Vertices[rn]->Z) << poly->ZDecompress;
    interpy = y + (rslope >= (1<<18));
    persp = SWRen_CheckPerspectiveLerp(wc, wn, true);
    u32 wr = SWRen_Interpolate(interpy, yc, yn, wc, wn, wc, wn, true, persp, false);
    s32 zr = SWRen_Interpolate(interpy, yc, yn, wc, wn, zc, zn, true, gx->LatWBuffer, true);
    Colors cr;
    cr.R = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[rc]->Color.R, poly->Vertices[rn]->Color.R, true, persp, false);
    cr.G = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[rc]->Color.G, poly->Vertices[rn]->Color.G, true, persp, false);
    cr.B = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[rc]->Color.B, poly->Vertices[rn]->Color.B, true, persp, false);
    s16 sr = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[rc]->S, poly->Vertices[rn]->S, true, persp, false);
    s16 tr = SWRen_Interpolate(interpy, yc, yn, wc, wn, poly->Vertices[rc]->T, poly->Vertices[rn]->T, true, persp, false);

    rs+=1;
    re+=1;

    if (ls < 0) ls = 0;
    if (le <= ls) le = ls+1;
    //if (rs >= re) rs = re-1;

    persp = SWRen_CheckPerspectiveLerp(wl, wr, false);

    s16 x = ls;
    s32 z;
    Colors color;
    s16 s, t;
    u8 talpha;
    Colors tcolor;
    bool lfill = true;
    bool rfill = true;
    bool cfill = true;

    attr.EdgeFlags = 0;
    if      (lslope >  (1<<18)) attr.BotXMajor  = true;
    else if (lslope < -(1<<18)) attr.TopXMajor  = true;
    else                        attr.LeftYMajor = true;
    s16 end = le;
    DS_CLAMP(end, >, re)
    DS_CLAMP(end, >, 256)
    for (; x < end; x++)
    {
        z = SWRen_Interpolate(x, ls, re, wl, wr, zl, zr, false, gx->LatWBuffer, true);
        color.R = SWRen_Interpolate(x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        s = SWRen_Interpolate(x, ls, re, wl, wr, sl, sr, false, persp, false);
        t = SWRen_Interpolate(x, ls, re, wl, wr, tl, tr, false, persp, false);

        // checkme: can stencil polygons use textures?
        if (gx->LatRasterCR.Texture && poly->TexAttr.Format)
            tcolor = SWRen_DecodeTextures(sys, poly, s, t, &talpha);
        else { tcolor.RGB = color.RGB >> 3; talpha = poly->Attrs.Alpha; }

        attr.AACov = lcov >> 5;
        lcov += lcovinc;

        SWRen_RasterizePixel(gx, poly, x, y, z, color, tcolor, talpha, attr, lfill);
    }

    attr.EdgeFlags = 0;
    if      (y >= (poly->Bot-1)) attr.BotXMajor  = true;
    else if (y == poly->Top) attr.TopXMajor  = true;

    end = rs;
    DS_CLAMP(end, >, re)
    DS_CLAMP(end, >, 256)
    for (; (x < end) && (x < 256); x++)
    {
        z = SWRen_Interpolate(x, ls, re, wl, wr, zl, zr, false, gx->LatWBuffer, true);
        color.R = SWRen_Interpolate(x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        s = SWRen_Interpolate(x, ls, re, wl, wr, sl, sr, false, persp, false);
        t = SWRen_Interpolate(x, ls, re, wl, wr, tl, tr, false, persp, false);

        // checkme: can stencil polygons use textures?
        if (gx->LatRasterCR.Texture && poly->TexAttr.Format)
            tcolor = SWRen_DecodeTextures(sys, poly, s, t, &talpha);
        else { tcolor.RGB = color.RGB >> 3; talpha = poly->Attrs.Alpha; }

        attr.AACov = 0x1F;

        SWRen_RasterizePixel(gx, poly, x, y, z, color, tcolor, talpha, attr, cfill);
    }

    attr.EdgeFlags = 0;
    if      (rslope < -(1<<18)) attr.BotXMajor   = true;
    else if (rslope >  (1<<18)) attr.TopXMajor   = true;
    else                        attr.RightYMajor = true;

    end = re;
    DS_CLAMP(end, >, 256)
    for (; x < end; x++)
    {
        z = SWRen_Interpolate(x, ls, re, wl, wr, zl, zr, false, gx->LatWBuffer, true);
        color.R = SWRen_Interpolate(x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        s = SWRen_Interpolate(x, ls, re, wl, wr, sl, sr, false, persp, false);
        t = SWRen_Interpolate(x, ls, re, wl, wr, tl, tr, false, persp, false);

        // checkme: can stencil polygons use textures?
        if (gx->LatRasterCR.Texture && poly->TexAttr.Format)
            tcolor = SWRen_DecodeTextures(sys, poly, s, t, &talpha);
        else { tcolor.RGB = color.RGB >> 3; talpha = poly->Attrs.Alpha; }

        attr.AACov = rcov >> 5;
        rcov += rcovinc;

        SWRen_RasterizePixel(gx, poly, x, y, z, color, tcolor, talpha, attr, rfill);
    }
}

void SWRen_RasterizeScanline(struct Console* sys, u8 y)
{
    GX3D* gx = &sys->GX3D;
    for (int i = 0; i < gx->RenderPolyCount; i++)
    {
        if ((y >= gx->RenderPolyRAM[i].Top) && (y <= gx->RenderPolyRAM[i].Bot))
        {
            SWRen_RasterizePoly(sys, &gx->RenderPolyRAM[i], y);
        }
    }
}

void SWRen_ClearScanline(GX3D* gx, u8 y)
{
    for (int x = 0; x < 256; x++)
    {
        gx->ZBuf[true][y][x] = (gx->ZBuf[false][y][x] = (((gx->LatRearDepth+1) << ((gx->LatWBuffer) ? 9 : 8)) - 1)); // checkme
        gx->ABuf[true][y][x] = (gx->ABuf[false][y][x] = (AttrBuf){.PolygonID = gx->LatRearAttr.ID}); // checkme: does it default to front or back facing?

        Colors color = SWRen_RGB555to666((Colors){.R = gx->LatRearAttr.R, .G = gx->LatRearAttr.G, .B = gx->LatRearAttr.B});
        gx->CBuf[true][y][x] = (gx->CBuf[false][y][x] = (color.R | (color.G << 6) | (color.B << 12) | (gx->LatRearAttr.Alpha << 18)));
    }
}

void SWRen_PostProcessScanline(GX3D* gx, u8 y)
{
    if (gx->LatRasterCR.AntiAlias)
    {
        for (int x = 0; x < 256; x++)
        {
            u8 cov = gx->ABuf[false][y][x].AACov;

            cov += (cov != 0);
            u32 outc = 0;
            if ((gx->CBuf[true][y][x] >> 18) != 0)
            {
                for (int i = 0; i < 6*4; i+=6)
                {
                    u8 tc = (gx->CBuf[false][y][x] >> i) & 0x3F;
                    u8 bc = (gx->CBuf[true][y][x] >> i) & 0x3F;
                    outc |= (((tc * cov) + (bc * (32-cov))) / 32) << i;
                }
                gx->CBuf[false][y][x] = outc;
            }
        }
    }
}

void SWRen_RasterizerFrame(struct Console* sys)
{
    if (!sys->PowerCR9.GPURasterizerPower)
    {
        sys->RenderedLines+=192;
        return;
    }
    for (int y = 0; y < 192; y++)
    {
        SWRen_ClearScanline(&sys->GX3D, y);
        SWRen_RasterizeScanline(sys, y);
        SWRen_PostProcessScanline(&sys->GX3D, y);
        sys->RenderedLines = y+1;
    }
}

void SWRen_SetTarget(struct Console* sys, const timestamp now)
{
    if (sys->SWRenTarget < now)
        sys->SWRenTarget = now;
}

void SWRen_Sync(struct Console* sys, timestamp now)
{
    if (!sys->SWRenStart) return;
    SWRen_SetTarget(sys, now);
    while (sys->SWRenTimestamp < now) thrd_yield();
}

void SWRen_SyncRenderedLines(struct Console* sys, u8 y)
{
    while (sys->RenderedLines < y);
}

void SWRen_Wait(struct Console* sys, const timestamp now)
{
    while (now >= sys->SWRenTarget) thrd_yield();
}

void SWRen_Init(struct Console* sys, const timestamp now)
{
    if (sys->SWRenStart) return;
    sys->SWRenTarget = now;
    sys->SWRenTimestamp = now;
    sys->SWRenStart = true;
}

int SWRen_MainLoop(void* ptr)
{
    struct Console* sys = ptr;
    while (!sys->SWRenStart) thrd_yield();

    while (!sys->KillSWRen)
    {
        SWRen_Wait(sys, sys->SWRenTimestamp);
        SWRen_RasterizerFrame(sys);
        sys->SWRenTimestamp += Scanline_Cycles*263;
    }
    return 0;
}
