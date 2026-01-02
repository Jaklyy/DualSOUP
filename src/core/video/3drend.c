#include "../console.h"
#include "3d.h"




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

s32 SWRen_PerspectiveInterp(Polygon* poly, s16 x, const s16 xdiff, const s32 w0, const s32 w1, s32 a0, s32 a1, const bool yaxis)
{
    s32 inc; // note: this should probably be reused.
    u8 shift;
    if (yaxis)
    {
        shift = 9;

        // this is the same as what's done for interp along x just with weird shit being done to the Ws.
        // if this is confusing to read just look at the X dir
        u32 num = (x * (w0 >> 1)) << shift;
        u32 den = ((x * ((w0 + (~w1 & 1)) >> 1)) + ((xdiff-x) * (w1>>1)));

        if (den == 0) inc = 0;
        else inc = num / den;
    }
    else
    {
        shift = 8;
        u32 num = (x * w0) << shift;
        u32 den = ((x * w0) + ((xdiff-x) * w1));

        if (den == 0) inc = 0;
        else inc = num / den;
    }

    if (a0 < a1)
        return a0 + (((s64)(a1-a0) * inc) >> shift);
    else
        return a1 + (((s64)(a0-a1) * ((1<<shift)-inc)) >> shift);
}

bool SWRen_CheckPerspectiveLerp(const s32 w0, const s32 w1, const bool yaxis)
{
    u8 mask = yaxis ? 0x7E : 0x7F; // yaxis lerp discards lsb so we dont check it
    return !((w0 == w1) && !(w0 & mask) && !(w1 & mask));
}

s32 SWRen_Interpolate(Polygon* poly, s16 x, const s16 x0, const s16 x1, const s32 w0, const s32 w1, const s32 a0, const s32 a1, const bool yaxis, const bool persp, const bool borkedlerp)
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

bool SWRen_DepthTest_LessThan(const GX3D* gx, const u16 x, const u8 y, const u32 z, const u8 flag, const bool bot)
{
    return ((z < gx->ZBuf[bot][y][x]) || ((flag & gx->ABuf[bot][y][x].Raw) && (z <= gx->ZBuf[bot][y][x])));
}

Colors SWRen_RGB555to666(Colors color)
{
    // the cast makes it faster i swear.
    color.RGB = (color.RGB << 1) - ((s32x4)color.RGB > 0);
    return color;
}

void SWRen_RasterizePixel(GX3D* gx, Polygon* poly, u16 x, u8 y, u32 z, Colors color)
{
    bool bot = false;
    if (!SWRen_DepthTest_LessThan(gx, x, y, z, 0, bot))
    {
        bot = true;
        if (!SWRen_DepthTest_LessThan(gx, x, y, z, 0, bot))
        {
            return;
        }
    }

    gx->ZBuf[true][y][x] = gx->ZBuf[false][y][x];
    gx->CBuf[true][y][x] = gx->CBuf[false][y][x];

    gx->ZBuf[bot][y][x] = z;

    color = SWRen_RGB555to666(color);
    gx->CBuf[bot][y][x] = color.R | (color.G << 6) | (color.B << 12) | 0x1F << 18;
}

void SWRen_RasterizePoly(GX3D* gx, Polygon* poly, const u8 y)
{
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

        rs = re - 1;
        le = ls + 1;
        DS_SWAP(lc, rc)
        DS_SWAP(ln, rn)
    }

    u8 yc = poly->Vertices[lc]->Y;
    u8 yn = poly->Vertices[ln]->Y;
    s32 wc = poly->Vertices[lc]->W;
    s32 wn = poly->Vertices[ln]->W;
    u32 zc = poly->Vertices[lc]->Z << poly->ZDecompress;
    u32 zn = poly->Vertices[ln]->Z << poly->ZDecompress;
    bool persp = SWRen_CheckPerspectiveLerp(wc, wn, true);
    s32 wl = SWRen_Interpolate(poly, y, yc, yn, wc, wn, wc, wn, true, persp, false);
    u32 zl = SWRen_Interpolate(poly, y, yc, yn, wc, wn, zc, zn, true, gx->RenderWBuffer, false);
    Colors cl;
    cl.R = SWRen_Interpolate(poly, y, yc, yn, wc, wn, poly->Vertices[lc]->Color.R, poly->Vertices[ln]->Color.R, true, persp, false);
    cl.G = SWRen_Interpolate(poly, y, yc, yn, wc, wn, poly->Vertices[lc]->Color.G, poly->Vertices[ln]->Color.G, true, persp, false);
    cl.B = SWRen_Interpolate(poly, y, yc, yn, wc, wn, poly->Vertices[lc]->Color.B, poly->Vertices[ln]->Color.B, true, persp, false);

    yc = poly->Vertices[rc]->Y;
    yn = poly->Vertices[rn]->Y;
    wc = poly->Vertices[rc]->W;
    wn = poly->Vertices[rn]->W;
    zc = poly->Vertices[rc]->Z << poly->ZDecompress;
    zn = poly->Vertices[rn]->Z << poly->ZDecompress;
    persp = SWRen_CheckPerspectiveLerp(wc, wn, true);
    s32 wr = SWRen_Interpolate(poly, y, yc, yn, wc, wn, wc, wn, true, persp, false);
    u32 zr = SWRen_Interpolate(poly, y, yc, yn, wc, wn, zc, zn, true, gx->RenderWBuffer, false);
    Colors cr;
    cr.R = SWRen_Interpolate(poly, y, yc, yn, wc, wn, poly->Vertices[rc]->Color.R, poly->Vertices[rn]->Color.R, true, persp, false);
    cr.G = SWRen_Interpolate(poly, y, yc, yn, wc, wn, poly->Vertices[rc]->Color.G, poly->Vertices[rn]->Color.G, true, persp, false);
    cr.B = SWRen_Interpolate(poly, y, yc, yn, wc, wn, poly->Vertices[rc]->Color.B, poly->Vertices[rn]->Color.B, true, persp, false);

    if (le > re) le = re;
    if (ls < 0) ls = 0;
    if (le <= ls) le = ls+1;

    persp = SWRen_CheckPerspectiveLerp(wl, wr, false);

    s16 x = ls;
    u32 z;
    Colors color;
    for (; (x < le) && (x < 256); x++)
    {
        z = SWRen_Interpolate(poly, x, ls, re, wl, wr, zl, zr, false, gx->RenderWBuffer, true);
        z = (z << 1) | poly->Frontfacing;
        color.R = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }

    for (; (x <= rs) && (x < 256); x++)
    {
        z = SWRen_Interpolate(poly, x, ls, re, wl, wr, zl, zr, false, gx->RenderWBuffer, true);
        z = (z << 1) | poly->Frontfacing;
        color.R = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }

    for (; (x <= re) && (x < 256); x++)
    {
        z = SWRen_Interpolate(poly, x, ls, re, wl, wr, zl, zr, false, gx->RenderWBuffer, true);
        z = (z << 1) | poly->Frontfacing;
        color.R = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.R, cr.R, false, persp, false);
        color.G = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.G, cr.G, false, persp, false);
        color.B = SWRen_Interpolate(poly, x, ls, re, wl, wr, cl.B, cr.B, false, persp, false);
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }
}

void SWRen_RasterizeScanline(GX3D* gx, u8 y)
{
    for (int i = 0; i < gx->RenderPolyCount; i++)
    {
        if ((y >= gx->RenderPolyRAM[i].Top) && (y <= gx->RenderPolyRAM[i].Bot))
        {
            SWRen_RasterizePoly(gx, &gx->RenderPolyRAM[i], y);
        }
    }
}

void SWRen_ClearScanline(GX3D* gx, u8 y)
{
    for (int x = 0; x < 256; x++)
    {
        gx->ZBuf[false][y][x] = (gx->LatRearDepth << 9) + 0x1FE;
        gx->ZBuf[true][y][x] = (gx->LatRearDepth << 9) + 0x1FE;

        Colors color = SWRen_RGB555to666((Colors){.R = gx->LatRearAttr.R, .G = gx->LatRearAttr.B, .B = gx->LatRearAttr.G});
        gx->CBuf[false][y][x] = color.R | (color.B << 6) | (color.G << 12) | (gx->LatRearAttr.Alpha << 18);
        gx->CBuf[true][y][x] = color.R | (color.B << 6) | (color.G << 12) | (gx->LatRearAttr.Alpha << 18);
    }
}

void SWRen_RasterizerFrame(struct Console* sys)
{
    if (!sys->PowerCR9.GPURasterizerPower) return;
    for (int y = 0; y < 192; y++)
    {
        SWRen_ClearScanline(&sys->GX3D, y);
        SWRen_RasterizeScanline(&sys->GX3D, y);
    }
}
