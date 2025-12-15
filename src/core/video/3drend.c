#include "../console.h"
#include "3d.h"




void SWRen_CalcSlope(u16 x0, u16 x1, u8 y0, u8 y1, u8 y, s16* xstart, s16* xend, bool* xmajor)
{
    //printf("sl%i %i %i %i\n", x0, x1, y0, y1);
    s16 xlen = x1 - x0;
    u8 ylen = y1 - y0;

    s32 incr;
    if (ylen == 0)
    {
        incr = (1<<18) * xlen;
    }
    else
    {
        incr = (1<<18 / ylen) * xlen;
    }

    s32 xreal = x0 << 18;

    *xmajor = (incr > (1<<18));

    if (incr > (1<<18)) xreal += ((1<<18)/2);

    xreal += ((s32)y - y0) * incr;

    *xstart = xreal >> 18;
    *xend = ((xreal & 0xFFFFE00) + incr) >> 18;
}

void SWRen_FindSlope(Polygon* poly, u8 y, s16* xstart, s16* xend, bool* xmajor, u8* vcur, u8* vnex, const bool dir)
{
    // cursed note: for some reason the modulo operations must be done on separate lines or else the compiler will optimize them away
    // i dont fucking know honestly.
    *vcur = poly->VTop;
    *vnex = poly->VTop + ((poly->Frontfacing ^ dir) ? 1 : -1);
    *vnex %= poly->NumVert;

    while ((y >= poly->SlopeY[*vnex]) && (*vcur != poly->VBot))
    {
        *vcur = *vnex;
        if (poly->Frontfacing ^ dir)
        {
            *vnex = *vcur + 1;
        }
        else
        {
            *vnex = *vcur - 1;
        }
        *vnex %= poly->NumVert;
    }

    //printf("verts; %i %i %i\n", poly->Vertices[*vcur]->X, poly->Vertices[*vnex]->X, dir);

    SWRen_CalcSlope(poly->Vertices[*vcur]->X, poly->Vertices[*vnex]->X, poly->Vertices[*vcur]->Y, poly->Vertices[*vnex]->Y, y, xstart, xend, xmajor);
}

bool SWRen_DepthTest_LessThan(const GX3D* gx, const u16 x, const u8 y, const u32 z, const u8 flag, const bool bot)
{
    //printf("%i %i\n", z, gx->ZBuf[bot][y][x]);
    return ((z < gx->ZBuf[bot][y][x]) || ((flag & gx->ABuf[bot][y][x].Raw) && (z <= gx->ZBuf[bot][y][x])));
}

void SWRen_RasterizePixel(GX3D* gx, Polygon* poly, u16 x, u8 y, u32 z, Colors color)
{
    bool bot = false;
    if (!SWRen_DepthTest_LessThan(gx, x, y, z, 0, bot))
    {
        bot = true;
        if (!SWRen_DepthTest_LessThan(gx, x, y, z, 0, bot))
        {
            //printf("fail\n");
            return;
        }
    }

    gx->ZBuf[true][y][x] = gx->ZBuf[false][y][x];
    gx->CBuf[true][y][x] = gx->CBuf[false][y][x];

    gx->ZBuf[bot][y][x] = z;
    gx->CBuf[bot][y][x] = color.R | (color.G << 8) | (color.B << 16) | 0x1F << 24;
}

void SWRen_RasterizePoly(GX3D* gx, Polygon* poly, const u8 y)
{
    s16 ls, le, rs, re;
    bool lxmajor, rxmajor;
    u8 lc, ln, rc, rn;
    SWRen_FindSlope(poly, y, &ls, &le, &lxmajor, &lc, &ln, false);
    SWRen_FindSlope(poly, y, &rs, &re, &rxmajor, &rc, &rn, true);

    u32 z = (poly->Vertices[lc]->Z << 1) | poly->Frontfacing;
    Colors color = poly->Vertices[lc]->Color;

    //printf("%i %i %i %i\n", ls, le, rs, re);

    //AttrBuf attr = {.TXMajor};

    if (ls < 0) ls = 0;
    if (le <= ls) le = ls+1;

    //printf("%i %i %i %i\n", lc, ln, rc, rn);

    for (s16 x = ls; (x < le) && (x < 256); x++)
    {
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }

    for (s16 x = le; (x < rs) && (x < 256); x++)
    {
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }

    for (s16 x = rs; (x < re) && (x < 256); x++)
    {
        SWRen_RasterizePixel(gx, poly, x, y, z, color);
    }
}

void SWRen_RasterizeScanline(GX3D* gx, u8 y)
{
    for (int i = 0; i < gx->RenderPolyCount; i++)
    {
        if ((y >= gx->RenderPolyRAM[i].Top) && (y <= gx->RenderPolyRAM[i].Bot))
            SWRen_RasterizePoly(gx, &gx->RenderPolyRAM[i], y);
    }
}

void SWRen_ClearScanline(GX3D* gx, u8 y)
{
    for (int x = 0; x < 256; x++)
    {
        gx->ZBuf[false][y][x] = gx->LatRearDepth << 1;
        gx->ZBuf[true][y][x] = gx->LatRearDepth << 1;
        gx->CBuf[false][y][x] = gx->LatRearAttr.R | (gx->LatRearAttr.B << 8) | (gx->LatRearAttr.G << 16) | (gx->LatRearAttr.Alpha << 24);
        gx->CBuf[true][y][x] = gx->LatRearAttr.R | (gx->LatRearAttr.B << 8) | (gx->LatRearAttr.G << 16) | (gx->LatRearAttr.Alpha << 24);
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
