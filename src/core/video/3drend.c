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

    u32 z = (poly->Vertices[lc]->Z << 1) | poly->Frontfacing;
    Colors color = SWRen_RGB555to666(poly->Vertices[lc]->Color);


    if (le > re) le = re;
    if (ls < 0) ls = 0;
    if (le <= ls) le = ls+1;

    s16 x = ls;
    for (; (x < le) && (x < 256); x++)
    {
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }

    for (; (x <= rs) && (x < 256); x++)
    {
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }

    for (; (x <= re) && (x < 256); x++)
    {
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
        gx->ZBuf[false][y][x] = gx->LatRearDepth << 1;
        gx->ZBuf[true][y][x] = gx->LatRearDepth << 1;

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
