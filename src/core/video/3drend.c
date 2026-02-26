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
    bool any = false;
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

s32 SWRen_CalcSlope(u16 x0, u16 x1, u8 y0, u8 y1, u8 y, s16* xstart, s16* xend, const bool dir)
{
    s16 xlen = x1 - x0;
    u8 ylen = y1 - y0;

    s32 slope;
    if (ylen == 0)
    {
        slope = (1<<18) * xlen;
    }
    else
    {
        slope = ((1<<18) / ylen) * xlen;
    }

    // get start coordinate
    s32 xreal = (x0-dir) << 18;

    bool xmajor = (slope > (1<<18)) || (slope < -(1<<18));

    // xmajor slopes are shifted half a pixel to the right
    if (xmajor) xreal += ((1<<18)/2);

    // right facing slopes are stupid.
    if (dir) xreal += (1<<18);
    if (dir && !xmajor && (slope != 0)) xreal += (1<<18);

    // specific ymajor slopes must be incremented one time less for w/e reason.
    if (((dir && (slope > 0)) || (!dir && (slope < 0))) && !xmajor) xreal -= slope;

    // calc y coordinate
    xreal += (y - y0) * slope;

    // calculate slope ranges
    if (slope < 0)
    {
        *xend = xreal >> 18;
        *xstart = ((xreal & 0xFFFFE00) + slope) >> 18;
    }
    else
    {
        *xstart = xreal >> 18;
        *xend = ((xreal & 0xFFFFE00) + slope) >> 18;
    }

    return slope;
}

s32 SWRen_FindSlope(Polygon* poly, u8 y, s16* xstart, s16* xend, u8* vcur, u8* vnex, const bool dir)
{
    // cursed note: for some reason the modulo operations must be done on separate lines or else the compiler will optimize them away
    // i dont fucking know honestly.
    *vcur = poly->VTop;
    if (poly->Frontfacing ^ dir)
    {
        *vnex = (*vcur + 1) % poly->NumVert;
    }
    else
    {
        *vnex = *vcur - 1;
        if (*vnex >= poly->NumVert) *vnex = poly->NumVert-1;
    }
    *vnex %= poly->NumVert;

    while ((y >= poly->SlopeY[*vnex]) && (*vcur != poly->VBot))
    {
        *vcur = *vnex;
        if (poly->Frontfacing ^ dir)
        {
            *vnex = (*vcur + 1) % poly->NumVert;
        }
        else
        {
            *vnex = *vcur - 1;
            if (*vnex >= poly->NumVert) *vnex = poly->NumVert-1;
        }
    }

    return SWRen_CalcSlope(poly->Vertices[*vcur]->X, poly->Vertices[*vnex]->X, poly->Vertices[*vcur]->Y, poly->Vertices[*vnex]->Y, y, xstart, xend, dir);
}

s32 SWRen_PerspectiveInterp(Polygon* poly, s16 x, const s16 xdiff, const u32 w0, const u32 w1, s32 a0, s32 a1, const bool yaxis)
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

s32 SWRen_Interpolate(Polygon* poly, s16 x, const s16 x0, const s16 x1, const u32 w0, const u32 w1, const s32 a0, const s32 a1, const bool yaxis, const bool persp, const bool borkedlerp)
{
    x -= x0;
    s16 xdiff = x1-x0;
    if ((x == 0) || (xdiff == 0)) return a0;

    if (persp)
    {
        // perspective correct interp using W values
        return SWRen_PerspectiveInterp(poly, x, xdiff, w0, w1, a0, a1, yaxis);
    }
    else
    {
        if (borkedlerp)
        {
            // z interp along x seems to be buggy?
            // it loses a lot of precision based on how wide the polygon is...
            // this seems to be fairly close to what hardware actually does, as weird as it is.
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
    GX3D* gx = &sys->GX3D;

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

Colors SWRen_BlendColors(GX3D* gx, Polygon* poly, Colors color, Colors tcolor, u8 talpha, u8* outalpha)
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

bool SWRen_DepthTest(const GX3D* gx, const bool equaldt, const u16 x, const u8 y, u32 z, const AttrBuf attr, const bool bot)
{
    AttrBuf attrbuf = gx->ABuf[bot][y][x];
    u32 zbuf = gx->ZBuf[bot][y][x];

    if (equaldt)
    {
        // checkme: this is probably slightly wrong.
        u32 diff = abs((s32)(zbuf - z));
        return (diff <= 256);
    }
    else
    {
        // in certain cases the depth test is actually less than or equals
        if ((attr.Frontfacing && !attrbuf.Frontfacing) // frontfacing > backfacing
            || ((attr.TopXMajor && gx->ABuf[bot][y][x].BotXMajor) // top xmajor > bot. xmajor
            || (attr.LeftYMajor && gx->ABuf[bot][y][x].RightYMajor))) // left y major > right y major
            return (z <= zbuf);
        else
            return (z < zbuf);
    }
}

void SWRen_RasterizePixel(GX3D* gx, Polygon* poly, u16 x, u8 y, u32 z, Colors color, Colors tcolor, u8 talpha, AttrBuf attr)
{
    bool bot = false;
    if (!SWRen_DepthTest(gx, poly->Attrs.EqualDepthTest, x, y, z, attr, bot))
    {
        bot = true;
        if (!SWRen_DepthTest(gx, poly->Attrs.EqualDepthTest, x, y, z, attr, bot))
        {
            return;
        }
    }

    u8 finalpha;
    Colors fincolor = SWRen_BlendColors(gx, poly, color, tcolor, talpha, &finalpha);

    if (finalpha < 1) return;

    gx->ZBuf[true][y][x] = gx->ZBuf[false][y][x];
    gx->CBuf[true][y][x] = gx->CBuf[false][y][x];
    gx->ABuf[true][y][x] = gx->ABuf[false][y][x];

    gx->ZBuf[bot][y][x] = z;
    gx->CBuf[bot][y][x] = fincolor.R | (fincolor.G << 6) | (fincolor.B << 12) | 0x1F << 18;
    gx->ABuf[bot][y][x] = attr;
}

void SWRen_RasterizePoly(struct Console* sys, Polygon* poly, const u8 y)
{
    GX3D* gx = &sys->GX3D;
    if ((y == poly->Bot) && (y != poly->Top)) return; // checkme: timings?

    s16 ls, le, rs, re;
    s32 lslope, rslope;
    u8 lc, ln, rc, rn;
    lslope = SWRen_FindSlope(poly, y, &ls, &le, &lc, &ln, false);
    rslope = SWRen_FindSlope(poly, y, &rs, &re, &rc, &rn, true);

    re--;
    rs--;
    if (rs >= re) rs = re-1;

    if (ls > re)
    {
        DS_SWAP(ls, re)
        DS_SWAP(lslope, rslope) // checkme?

        rs = re - 1;
        le = ls + 1;
        DS_SWAP(lc, rc)
        DS_SWAP(ln, rn)
    }

    u8 yc = poly->Vertices[lc]->Y;
    u8 yn = poly->Vertices[ln]->Y;
    u32 wc = poly->W[lc];
    u32 wn = poly->W[ln];
    u32 zc = (gx->RenderWBuffer) ? poly->W[lc] << poly->ZDecompress : poly->Vertices[lc]->Z << poly->ZDecompress;
    u32 zn = (gx->RenderWBuffer) ? poly->W[ln] << poly->ZDecompress : poly->Vertices[ln]->Z << poly->ZDecompress;
    u8 interpy = y + (lslope <= -(1<<18));
    bool persp = SWRen_CheckPerspectiveLerp(wc, wn, true);
    u32 wl = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, wc, wn, true, persp, false);
    u32 zl = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, zc, zn, true, gx->RenderWBuffer, false);
    Colors cl;
    cl.R = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[lc]->Color.R, poly->Vertices[ln]->Color.R, true, persp, false);
    cl.G = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[lc]->Color.G, poly->Vertices[ln]->Color.G, true, persp, false);
    cl.B = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[lc]->Color.B, poly->Vertices[ln]->Color.B, true, persp, false);
    s16 sl = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[lc]->S, poly->Vertices[ln]->S, true, persp, false);
    s16 tl = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[lc]->T, poly->Vertices[ln]->T, true, persp, false);

    yc = poly->Vertices[rc]->Y;
    yn = poly->Vertices[rn]->Y;
    wc = poly->W[rc];
    wn = poly->W[rn];
    zc = (gx->RenderWBuffer) ? poly->W[rc] << poly->ZDecompress : poly->Vertices[rc]->Z << poly->ZDecompress;
    zn = (gx->RenderWBuffer) ? poly->W[rn] << poly->ZDecompress : poly->Vertices[rn]->Z << poly->ZDecompress;
    interpy = y + (rslope >= (1<<18));
    persp = SWRen_CheckPerspectiveLerp(wc, wn, true);
    u32 wr = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, wc, wn, true, persp, false);
    u32 zr = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, zc, zn, true, gx->RenderWBuffer, false);
    Colors cr;
    cr.R = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[rc]->Color.R, poly->Vertices[rn]->Color.R, true, persp, false);
    cr.G = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[rc]->Color.G, poly->Vertices[rn]->Color.G, true, persp, false);
    cr.B = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[rc]->Color.B, poly->Vertices[rn]->Color.B, true, persp, false);
    s16 sr = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[rc]->S, poly->Vertices[rn]->S, true, persp, false);
    s16 tr = SWRen_Interpolate(poly, interpy, yc, yn, wc, wn, poly->Vertices[rc]->T, poly->Vertices[rn]->T, true, persp, false);

    if (le > re) le = re;
    if (ls < 0) ls = 0;
    if (le <= ls) le = ls+1;

    rs+=1;
    re+=1;

    persp = SWRen_CheckPerspectiveLerp(wl, wr, false);

    s16 x = ls;
    u32 z;
    Colors color;
    s16 s, t;
    u8 talpha;
    Colors tcolor;
    AttrBuf attr;
    attr.Frontfacing = poly->Frontfacing;

    attr.EdgeFlags = 0;
    if      (lslope >  (1<<18)) attr.BotXMajor  = true;
    else if (lslope < -(1<<18)) attr.TopXMajor  = true;
    else                        attr.LeftYMajor = true;
    for (; (x < le) && (x < 256); x++)
    {
        z = SWRen_Interpolate(poly, x, ls, re, wl, wr, zl, zr, false, gx->RenderWBuffer, true);
        color.R = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        s = SWRen_Interpolate(poly, x, ls, re, wl, wr, sl, sr, false, persp, false);
        t = SWRen_Interpolate(poly, x, ls, re, wl, wr, tl, tr, false, persp, false);

        if (gx->LatRasterCR.Texture && poly->TexAttr.Format)
            tcolor = SWRen_DecodeTextures(sys, poly, s, t, &talpha);
        else { tcolor.RGB = color.RGB >> 3; talpha = poly->Attrs.Alpha; }

        SWRen_RasterizePixel(gx, poly, x, y, z, color, tcolor, talpha, attr);
    }

    attr.EdgeFlags = 0;
    if      (y == poly->Bot) attr.BotXMajor  = true;
    else if (y == poly->Top) attr.TopXMajor  = true;
    for (; (x < rs) && (x < 256); x++)
    {
        z = SWRen_Interpolate(poly, x, ls, re, wl, wr, zl, zr, false, gx->RenderWBuffer, true);
        color.R = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        s = SWRen_Interpolate(poly, x, ls, re, wl, wr, sl, sr, false, persp, false);
        t = SWRen_Interpolate(poly, x, ls, re, wl, wr, tl, tr, false, persp, false);

        if (gx->LatRasterCR.Texture && poly->TexAttr.Format)
            tcolor = SWRen_DecodeTextures(sys, poly, s, t, &talpha);
        else { tcolor.RGB = color.RGB >> 3; talpha = poly->Attrs.Alpha; }

        SWRen_RasterizePixel(gx, poly, x, y, z, color, tcolor, talpha, attr);
    }

    attr.EdgeFlags = 0;
    if      (rslope < -(1<<18)) attr.BotXMajor   = true;
    else if (rslope >  (1<<18)) attr.TopXMajor   = true;
    else                        attr.RightYMajor = true;
    for (; (x < re) && (x < 256); x++)
    {
        z = SWRen_Interpolate(poly, x, ls, re, wl, wr, zl, zr, false, gx->RenderWBuffer, true);
        color.R = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        s = SWRen_Interpolate(poly, x, ls, re, wl, wr, sl, sr, false, persp, false);
        t = SWRen_Interpolate(poly, x, ls, re, wl, wr, tl, tr, false, persp, false);

        if (gx->LatRasterCR.Texture && poly->TexAttr.Format)
            tcolor = SWRen_DecodeTextures(sys, poly, s, t, &talpha);
        else { tcolor.RGB = color.RGB >> 3; talpha = poly->Attrs.Alpha; }

        SWRen_RasterizePixel(gx, poly, x, y, z, color, tcolor, talpha, attr);
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
        u32 z = ((gx->RenderWBuffer) ? ((gx->LatRearDepth << 9) + 0x1FF) : ((gx->LatRearDepth << 8) + 0xFF));
        gx->ZBuf[false][y][x] = z;
        gx->ZBuf[true][y][x] = z;

        Colors color = SWRen_RGB555to666((Colors){.R = gx->LatRearAttr.R, .G = gx->LatRearAttr.G, .B = gx->LatRearAttr.B});
        gx->CBuf[false][y][x] = color.R | (color.G << 6) | (color.B << 12) | (gx->LatRearAttr.Alpha << 18);
        gx->CBuf[true][y][x] = color.R | (color.G << 6) | (color.B << 12) | (gx->LatRearAttr.Alpha << 18);
    }
}

void SWRen_RasterizerFrame(struct Console* sys)
{
    if (!sys->PowerCR9.GPURasterizerPower) return;
    for (int y = 0; y < 192; y++)
    {
        SWRen_ClearScanline(&sys->GX3D, y);
        SWRen_RasterizeScanline(sys, y);
    }
}
