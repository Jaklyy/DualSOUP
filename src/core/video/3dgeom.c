#include <stdbit.h>
#include <stdlib.h>
#include "../console.h"
#include "3d.h"




Matrix GX_MatrixMultiply(Matrix a, Matrix b)
{
    Matrix Out;
    Out.Row[0] = ((b.Arr[0] * a.Row[0]) + (b.Arr[1] * a.Row[1]) + (b.Arr[2] * a.Row[2]) + (b.Arr[3] * a.Row[3]));
    Out.Row[1] = ((b.Arr[4] * a.Row[0]) + (b.Arr[5] * a.Row[1]) + (b.Arr[6] * a.Row[2]) + (b.Arr[7] * a.Row[3]));
    Out.Row[2] = ((b.Arr[8] * a.Row[0]) + (b.Arr[9] * a.Row[1]) + (b.Arr[10] * a.Row[2]) + (b.Arr[11] * a.Row[3]));
    Out.Row[3] = ((b.Arr[12] * a.Row[0]) + (b.Arr[13] * a.Row[1]) + (b.Arr[14] * a.Row[2]) + (b.Arr[15] * a.Row[3]));
    Out.Vec = (Out.Vec << 20) >> 32;

    return Out;
}

void GX_MatrixScale(Matrix* a, Vector b)
{
    a->Row[0] = ((a->Row[0] * b.X ) << 20) >> 32;
    a->Row[1] = ((a->Row[1] * b.Y ) << 20) >> 32;
    a->Row[2] = ((a->Row[2] * b.Z ) << 20) >> 32;
}

void GX_MatrixTranslate(Matrix* a, Vector b)
{
    a->Row[3] += (((a->Row[0] * b.X) + (a->Row[1] * b.Y) + (a->Row[2] * b.Z)) << 20) >> 32;
}

void GX_UpdateClip(struct Console* sys)
{
    GX3D* gx = &sys->GX3D;

    if (gx->ClipDirty)
    {
        gx->ClipMatrix = GX_MatrixMultiply(gx->ProjectionMatrix, gx->PositionMatrix);
        gx->ClipDirty = false;
    }
}


#define Interp(var) \
    out.var = a->var - ((((s64)a->var - b->var) * recip) >> 24)

VertexTmp GX_InterpolateVertex(VertexTmp* a, VertexTmp* b, const u8 dir, const bool positive)
{
    s64 den, num, recip;

    if (positive)
    {
        num = a->Coords.W - a->Coords.Arr[dir];
        den = num - (b->Coords.W - b->Coords.Arr[dir]);
    }
    else
    {
        num = a->Coords.Arr[dir];
        den = num - b->Coords.Arr[dir];
    }

    if (!den) recip = num << 24; // checkme
    else recip = (num << 24) / den;

    VertexTmp out = {};

    if (dir != 0) Interp(Coords.X);
    if (dir != 1) Interp(Coords.Y);
    if (dir != 2) Interp(Coords.Z);
    Interp(Coords.W);
    out.Coords.Arr[dir] = ((positive) ? out.Coords.W : 0);

    Interp(Color.R);
    Interp(Color.G);
    Interp(Color.B);
    Interp(TexCoords[0]);
    Interp(TexCoords[1]);

    return out;
}

#undef Interp

PolygonTmp GX_ClipVertex(GX3D* gx, PolygonTmp* poly, unsigned* nvert, const int dir, const bool positive)
{
    PolygonTmp tmp;
    tmp.Clipped = poly->Clipped;
    unsigned cur = 0;
    for (unsigned i = 0; i < *nvert; i++)
    {
        if ((positive) ? (poly->Vertices[i].Coords.Arr[dir] > poly->Vertices[i].Coords.W)
                       : (poly->Vertices[i].Coords.Arr[dir] < 0))
        {
            // check if the polygon should be discarded entirely
            if ((dir == 2) && positive && !gx->CurPolyAttr.RenderFarPlaneClipped)
            {
                *nvert = 0;
                return tmp;
            }

            int prev = (i-1);
            if (prev < 0) prev = *nvert - 1;
            if ((positive) ? (poly->Vertices[prev].Coords.Arr[dir] <= poly->Vertices[prev].Coords.W)
                           : (poly->Vertices[prev].Coords.Arr[dir] >= 0))
            {
                tmp.Vertices[cur++] = GX_InterpolateVertex(&poly->Vertices[i], &poly->Vertices[prev], dir, positive);
                tmp.Clipped = true;
            }

            unsigned next = (i+1);
            if (next >= *nvert) next = 0;
            if ((positive) ? (poly->Vertices[next].Coords.Arr[dir] <= poly->Vertices[next].Coords.W)
                           : (poly->Vertices[next].Coords.Arr[dir] >= 0))
            {
                tmp.Vertices[cur++] = GX_InterpolateVertex(&poly->Vertices[next], &poly->Vertices[i], dir, positive);
                tmp.Clipped = true;
            }
        }
        else tmp.Vertices[cur++] = poly->Vertices[i];
    }

    *nvert = cur;
    return tmp;
}

void GX_ClipPolygon(GX3D* gx, PolygonTmp* poly, unsigned* nvert)
{
    *poly = GX_ClipVertex(gx, poly, nvert, 2, true);
    *poly = GX_ClipVertex(gx, poly, nvert, 2, false);
    *poly = GX_ClipVertex(gx, poly, nvert, 1, true);
    *poly = GX_ClipVertex(gx, poly, nvert, 1, false);
    *poly = GX_ClipVertex(gx, poly, nvert, 0, true);
    *poly = GX_ClipVertex(gx, poly, nvert, 0, false);
}

void GX_FinalizePolygon(struct Console* sys, unsigned nvert, bool* boxtestres)
{
    GX3D* gx = &sys->GX3D;
    PolygonTmp poly = gx->PolygonTmp;
    bool frontfacing;

    // calculate dot product to determine polygon facing direction for culling and rasterization

    if (boxtestres == nullptr)
    {
        Vector v0, v1;
        v0.Vec = poly.Vertices[0].Coords.Vec - poly.Vertices[2].Coords.Vec;
        v1.Vec = poly.Vertices[1].Coords.Vec - poly.Vertices[2].Coords.Vec;

        // for some reason if one of the vectors is entirely 0 its automatically accepted, even if neither face is allowed to render, and is treated as frontfacing for rasterization.
        // im not entirely sure why though... possibly to do with the normalization step?
        // or maybe they do some weird-ass algorithm instead of doing things normally?
        if (((v0.X|v0.Y|v0.W) == 0) || ((v1.X|v1.Y|v1.W) == 0))
        {
            frontfacing = true;
        }
        else
        {
            // calculate cross product
            Vector cross; cross.Vec = ((Vec){v0.Y, v0.W, 0, v0.X} * (Vec){v1.W, v1.X, 0, v1.Y}) - ((Vec){v0.W, v0.X, 0, v0.Y} * (Vec){v1.Y, v1.W, 0, v1.X});

            // TODO: precision is lost here and im not quite sure how they do it exactly
            // unfortunately if we dont do anything here we encounter integer overflows so we're just gonna do what melonDS does for the time being.
            while(((cross.X >> 31) ^ (cross.X >> 63)) || ((cross.Y >> 31) ^ (cross.Y >> 63)) || ((cross.W >> 31) ^ (cross.W >> 63)))
                cross.Vec >>= 4;

            // calc dot product
            // supposedly we can use any vertex as a "camera vector" im not sure how that works, but sure.
            // going with vertex 2 based on my gut, since hw was using that as the center vertex.
            cross.Vec *= poly.Vertices[2].Coords.Vec ; // checkme: do i actually need to removed the added W here?

            s64 dot = cross.X + cross.Y + cross.W;

            frontfacing = (dot <= 0);

            //printf("front: %i\n", frontfacing);
            // if the dot is 0 then it will render if either front or back are enabled.
            if (!(((dot <= 0) && gx->CurPolyAttr.RenderFront) || ((dot >= 0) && gx->CurPolyAttr.RenderBack)))
            {
                gx->SharedVtx[0] = nullptr;
                return;
            }
        }
    }

    GX_ClipPolygon(gx, &poly, &nvert);

    if (boxtestres != nullptr)
    {
        *boxtestres = nvert;
        return;
    }

    if ((nvert == 0) || (gx->PolyRAMPtr >= 2048))
    {
        gx->SharedVtx[0] = nullptr;
        return;
    }

    if (poly.Clipped)
    {
        gx->SharedVtx[0] = nullptr;
    }

    Polygon fin;

    unsigned vtxadded = 0;
    {
        unsigned vtx = 0;
        if (gx->SharedVtx[0] != nullptr)
        {
            vtx = 2;
            fin.Vertices[0] = gx->SharedVtx[0];
            fin.Vertices[1] = gx->SharedVtx[1];
        }

        // allocate remaining vertices
        for (; vtx < nvert; vtx++, vtxadded++)
        {
            if ((gx->VtxRAMPtr + vtxadded) >= 6144) // checkme...?
            {
                gx->SharedVtx[0] = nullptr;
                return;
            }
            fin.Vertices[vtx] = &gx->GXVtxRAM[gx->VtxRAMPtr+vtxadded];
        }
    }

    // all vertices are always updated, even ones that were already created by a previous polygon in a strip.
    // this allows for a bug when manipulating the viewport mid-strip.
    for (unsigned i = 0; i < nvert; i++)
    {
        poly.Vertices[i].Coords.W &= 0x1FFFFFF;

        s64 x = poly.Vertices[i].Coords.X, y = poly.Vertices[i].Coords.Y;
        u32 w = poly.Vertices[i].Coords.W;


        // w can easily be 0 here
        if (w == 0)
        {
            // checkme
            fin.Vertices[i]->X = 0;
            fin.Vertices[i]->Y = 0;
        }
        else
        {
            x *= gx->ViewportWidth;
            y *= gx->ViewportHeight;

            if (w > 0x1FFFF)
            {
                x >>= 2;
                y >>= 2;
                w >>= 2;
            }

            fin.Vertices[i]->X = ((x / w) + gx->ViewportLeft) & 0x1FF;
            fin.Vertices[i]->Y = ((y / w) + gx->ViewportTop) & 0xFF;
        }
    }

    // zero dot

    // melonds does timings here ig?

    // set up for next polygon in a strip
    if (poly.Clipped)
    {
        gx->SharedVtx[0] = nullptr;
    }
    else if (gx->PolygonType == Poly_TriStrip)
    {
        if (gx->TriStripOdd)
        {
            gx->SharedVtx[0] = fin.Vertices[0];
            gx->SharedVtx[1] = fin.Vertices[2];
        }
        else
        {
            gx->SharedVtx[0] = fin.Vertices[2];
            gx->SharedVtx[1] = fin.Vertices[1];
        }
    }
    else if (gx->PolygonType == Poly_QuadStrip)
    {
        gx->SharedVtx[0] = fin.Vertices[3];
        gx->SharedVtx[1] = fin.Vertices[2];
    }

    fin.Frontfacing = frontfacing;
    fin.Attrs = gx->CurPolyAttr;
    fin.Trans = ((fin.Attrs.Alpha > 0) && (fin.Attrs.Alpha < 31)); // TODO: Texture params impact this!!
    fin.NumVert = nvert;
    fin.TexAttr = gx->TexAttr;
    fin.TexPal = gx->TexPal;

    s32 ytop = 193, ybot = -1, vtop = 0, vbot = 0;

    int wsize = 1;
    for (unsigned i = 0; i < nvert; i++)
    {
        fin.Vertices[i]->Color.RGB = (poly.Vertices[i].Color.RGB << 4) + (((s32x4)poly.Vertices[i].Color.RGB > 0) & (u32x4){0xF, 0xF, 0xF, 0xF});

        fin.SlopeY[i] = fin.Vertices[i]->Y;
        fin.Vertices[i]->S = poly.Vertices[i].TexCoords[0];
        fin.Vertices[i]->T = poly.Vertices[i].TexCoords[1];

        if (fin.Vertices[i]->Y < ytop)
        {
            ytop = fin.Vertices[i]->Y;
            vtop = i;
        }
        if (fin.Vertices[i]->Y > ybot)
        {
            ybot = fin.Vertices[i]->Y;
            vbot = i;
        }

        // get value to normal W with
        /*int temp = ((32 - stdc_leading_zeros(poly.Vertices[i].Coords.W>>1)) / 4) * 4;

        if (wsize < temp)
        {
            wsize = temp;
        }*/
        while((poly.Vertices[i].Coords.W >> wsize))// && (wsize < 32))
            wsize += 4;
    }
    wsize -= 1;

    for (unsigned i = 0; i < nvert; i++)
    {
        // normalize W to fit into 16 bits
        u32 wnorm = poly.Vertices[i].Coords.W >> 1;
        if (wsize < 16)
        {
            wnorm <<= (16-wsize);
            fin.W[i] = wnorm;
            wnorm >>= (16-wsize);
            fin.ZDecompress = 0;
        }
        else
        {
            wnorm >>= (wsize - 16);
            fin.W[i] = wnorm;
            fin.ZDecompress = (wsize - 16);
        }


        // if we are not using wbuffering we need to calc the Z
        // otherwise we just use the W
        if (!gx->WBuffer)
        {
            // compress Z into 16 bits
            if (poly.Vertices[i].Coords.W != 0)
            {
                fin.Vertices[i]->Z = ((poly.Vertices[i].Coords.Z * 0x4000) / poly.Vertices[i].Coords.W);
            }
            else fin.Vertices[i]->Z = 0;

            fin.Vertices[i]->Z += 0x3FFF;

            if (fin.Vertices[i]->Z < 0) 
                fin.Vertices[i]->Z = 0;
            else if (fin.Vertices[i]->Z > 0x7FFF)
            {
                fin.Vertices[i]->Z = 0x7FFF;
            }
            fin.ZDecompress = 8;
        }
        else
        {
            // checkme: does the "true" Z get used for rendering in any way with Wbuffering?
            fin.Vertices[i]->Z = wnorm;
        }
    }

    fin.VTop = vtop;
    fin.VBot = vbot;
    fin.Top = ytop;
    fin.Bot = ybot;

    // transparent polygons are sorted last, and ties are broken based on initial index
    fin.SortKey = (fin.Trans << 31) | (gx->PolyRAMPtr);
    // sort by polygon top and bottom, can opt out for transparent polygons.
    if (!fin.Trans || !gx->ManualTransSort)
        fin.SortKey |= ((ybot << 8) | ytop) << 12;

    gx->GXPolyRAM[gx->PolyRAMPtr] = fin;

    gx->VtxRAMPtr += vtxadded;
    gx->PolyRAMPtr++;
}

void GX_SubmitVertex(struct Console* sys, bool* boxtestres, const bool postest)
{
    GX3D* gx = &sys->GX3D;
    GX_UpdateClip(sys);

    gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Vec = ((gx->TmpVertex.X * gx->ClipMatrix.Row[0]) + (gx->TmpVertex.Y * gx->ClipMatrix.Row[1]) + (gx->TmpVertex.Z * gx->ClipMatrix.Row[2]) + (gx->TmpVertex.W * gx->ClipMatrix.Row[3])) >> 12;

    if (postest)
    {
        gx->PosTestRes[0] = gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.X;
        gx->PosTestRes[1] = gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Y;
        gx->PosTestRes[2] = gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Z;
        gx->PosTestRes[3] = gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.W;
        return;
    }

    // CHECKME: save a copy of the coordinates for polygon orientation calcs
    //gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].CoordsInitial.Vec = gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Vec;

    // y needs to be negated
    gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Y = -gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Y;

    // all vertex coordinates are incremented by W
    gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.Vec += gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Coords.W;

    gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].Color = gx->VertexColor;

    if (gx->TexAttr.CoordTransMode == 3)
    {
        gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].TexCoords[0] = (((gx->TmpVertex.X * gx->TextureMatrix.Arr[0]) + (gx->TmpVertex.Y * gx->TextureMatrix.Arr[4]) + (gx->TmpVertex.Z * gx->TextureMatrix.Arr[8])) >> 24) + gx->TexCoords[0];
        gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].TexCoords[1] = (((gx->TmpVertex.X * gx->TextureMatrix.Arr[1]) + (gx->TmpVertex.Y * gx->TextureMatrix.Arr[5]) + (gx->TmpVertex.Z * gx->TextureMatrix.Arr[9])) >> 24) + gx->TexCoords[1];
    }
    else
    {
        gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].TexCoords[0] = gx->TransTexCoords[0];
        gx->PolygonTmp.Vertices[gx->TmpPolygonPtr].TexCoords[1] = gx->TransTexCoords[1];
    }

    gx->TmpPolygonPtr++;
    gx->PartialPolygon = true;
    switch(gx->PolygonType)
    {
    case Poly_Tri:
    {
        if (gx->TmpPolygonPtr == 3)
        {
            gx->TmpPolygonPtr = 0;
            gx->PartialPolygon = false;
            GX_FinalizePolygon(sys, 3, nullptr);
        }
        break;
    }
    case Poly_Quad:
    {
        if (gx->TmpPolygonPtr == 4)
        {
            gx->TmpPolygonPtr = 0;
            gx->PartialPolygon = false;
            GX_FinalizePolygon(sys, 4, boxtestres);
        }
        break;
    }
    case Poly_TriStrip:
    {
        if (gx->TmpPolygonPtr == 3)
        {
            gx->TmpPolygonPtr = 2;
            gx->PartialPolygon = false;
            if (gx->TriStripOdd)
            {
                DS_SWAP(gx->PolygonTmp.Vertices[1], gx->PolygonTmp.Vertices[0]);
                GX_FinalizePolygon(sys, 3, nullptr);
            }
            else
            {
                GX_FinalizePolygon(sys, 3, boxtestres);
                gx->PolygonTmp.Vertices[0] = gx->PolygonTmp.Vertices[1];
            }
            gx->PolygonTmp.Vertices[1] = gx->PolygonTmp.Vertices[2];
            gx->TriStripOdd = !gx->TriStripOdd;
        }
        break;
    }
    case Poly_QuadStrip:
    {
        if (gx->TmpPolygonPtr == 4)
        {
            gx->TmpPolygonPtr = 2;
            gx->PartialPolygon = false;
            DS_SWAP(gx->PolygonTmp.Vertices[2], gx->PolygonTmp.Vertices[3])
            GX_FinalizePolygon(sys, 4, nullptr);
            gx->PolygonTmp.Vertices[0] = gx->PolygonTmp.Vertices[3];
            gx->PolygonTmp.Vertices[1] = gx->PolygonTmp.Vertices[2];
        }
        break;
    }
    }

}

void GX_UpdateNormal(struct Console* sys, const u32 param, const bool vectest)
{
    GX3D* gx = &sys->GX3D;

    // s1.9
    s64 dir[3] = {(s64)((s32)(param >>  0) << 22) >> 22,
                  (s64)((s32)(param >> 10) << 22) >> 22,
                  (s64)((s32)(param >> 20) << 22) >> 22};

    if (!vectest && (gx->TexAttr.CoordTransMode == 2))
    {
        gx->TransTexCoords[0] = (((dir[0] * gx->TextureMatrix.Arr[0]) + (dir[1] * gx->TextureMatrix.Arr[4]) + (dir[2] * gx->TextureMatrix.Arr[8])) >> 21) + gx->TexCoords[0];
        gx->TransTexCoords[1] = (((dir[0] * gx->TextureMatrix.Arr[1]) + (dir[1] * gx->TextureMatrix.Arr[5]) + (dir[2] * gx->TextureMatrix.Arr[9])) >> 21) + gx->TexCoords[1];
    }

    // translate normal vector
    Vector normal; normal.Vec = (dir[0] * gx->VectorMatrix.Row[0]) + (dir[1] * gx->VectorMatrix.Row[1]) + (dir[2] * gx->VectorMatrix.Row[2]);

    if (vectest)
    {
        // s12
        normal.Vec = normal.Vec << 40 >> 52;
        gx->VecTestRes[0] = normal.X;
        gx->VecTestRes[1] = normal.Y;
        gx->VecTestRes[2] = normal.Z;
        return;
    }

    // s11
    normal.Vec = normal.Vec << 41 >> 53;


    // initialize light with emissive color
    Colors color; color.RGB = gx->EmisColor.RGB << 14;

    // is it possible to parallelize each light calc efficiently?
    for (int l = 0; l < 4; l++)
    {
        if (!(gx->CurPolyAttr.LightEnables & (1<<l))) continue;

        // calculate dot product of light dir and normal
        // Note: for some reason they discard precision before adding, specifically here?
        // CHECKME: not sure how big the dot can be, might not be able to reach the limit...?
        Vector tmp; tmp.Vec = (gx->LightVec[l].Vec * normal.Vec) >> 9;
        s32 dot = tmp.X + tmp.Y + tmp.Z;

        s32 speclevel;
        // i wanna say the dot gets clamped to min 0?
        if (dot > 0)
        {
            // checkme: this should be safe to discard for specular too?
            // I can't remember why i didn't do that for melonDS's implementation.
            // s1.10 i think
            dot = (dot << 21) >> 21;
            // truncate diffuse color to u20 before adding to final color.
            color.RGB += (gx->DiffColor.RGB * gx->LightColor[l].RGB * dot) & 0xFFFFF;

            // calc specular level

            // add the z component of the normal
            dot += normal.Z;

            // truncate to s11 again...
            dot = dot << 21 >> 21;
            // square dot and truncate to u10
            dot = ((dot * dot) >> 10) & 0x3FF;

            // multiply dot by reciprocal and subtract "1"
            speclevel = (dot * gx->LightRecip[l] >> 8) - (1<<9);

            // CHECKME: is this actually full s32?
            // clamp min 0?
            if (speclevel < 0) speclevel = 0;
            else
            {
                // i dont know why it does this.
                // surely this isn't actually what it does?
                // s14, then clamp value to min 0, max 511.
                speclevel = (speclevel << 18) >> 18;
                if (speclevel < 0) speclevel = 0;
                else if (speclevel > 0x1FF) speclevel = 0x1FF;
            }
        }
        else speclevel = 0;

        if (gx->UseSpecTable)
        {
            speclevel >>= 2;
            speclevel = gx->SpecTable[speclevel] << 1;
        }

        color.RGB += ((gx->SpecColor.RGB * speclevel) + (gx->AmbiColor.RGB << 9)) * gx->LightColor[l].RGB;
    }

    // remove fractional component
    color.RGB >>= 14;

    gx->VertexColor.R = (color.R > 31) ? 31 : color.R;
    gx->VertexColor.G = (color.G > 31) ? 31 : color.G;
    gx->VertexColor.B = (color.B > 31) ? 31 : color.B;
}

bool GX_PolygonBegin(GX3D* gx, const u8 type)
{
    // basically just run polygon begin
    gx->PolygonType = type;
    gx->CurPolyAttr = gx->NextPolyAttr;
    gx->SharedVtx[0] = nullptr;
    gx->TriStripOdd = false;
    gx->TmpPolygonPtr = 0;
    if (gx->PartialPolygon)
    {
        gx->ExecTS = timestamp_max;
        LogPrint(LOG_GX|LOG_EXCEP, "GX HANGING, UH OH\n");
    }
    return !gx->PartialPolygon;
}

void GX_BoxTest(struct Console* sys)
{
    GX3D* gx = &sys->GX3D;

    // todo: set busy flag, implement timings
    gx->Status.BoxTestRes = false; // checkme: is this cleared if it hangs?

    if (!GX_PolygonBegin(gx, Poly_Quad)) // hw might do a strip? CHECKME: do box tests actually update the polygon type?
        return; // dont bother finishing the calc if it hangs

    bool res = false;

    // checkme: box tests might return results as soon as they're found (although busy flag doesn't seem to get cleared early??), so the order of faces being tested probably matters?

    // x0 y0 z0; x1 y1 z0
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    if (res)
    {
        gx->Status.BoxTestRes = true;
        return;
    }

    // x0, y0, z0, x1, y0, z1
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    if (res)
    {
        gx->Status.BoxTestRes = true;
        return;
    }

    // x0, y0, z0, x0, y1, z1
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    if (res)
    {
        gx->Status.BoxTestRes = true;
        return;
    }

    // x0, y0, z1, x1, y1, z1
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    if (res)
    {
        gx->Status.BoxTestRes = true;
        return;
    }

    // x0, y1, z0, x1, y1, z1
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = gx->BoxTestParams[0];
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    if (res)
    {
        gx->Status.BoxTestRes = true;
        return;
    }

    // x1, y0, z0, x1, y1, z1
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = gx->BoxTestParams[2];
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = (s16)(gx->BoxTestParams[1] + gx->BoxTestParams[4]);
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    gx->TmpVertex.X = (s16)(gx->BoxTestParams[0] + gx->BoxTestParams[3]);
    gx->TmpVertex.Y = gx->BoxTestParams[1];
    gx->TmpVertex.Z = (s16)(gx->BoxTestParams[2] + gx->BoxTestParams[5]);
    GX_SubmitVertex(sys, &res, false);
    if (res)
    {
        gx->Status.BoxTestRes = true;
        return;
    }
}

bool GX_RunCommand(struct Console* sys, const timestamp now)
{
    GX3D* gx = &sys->GX3D;

    if (gx->CmdReady) // checkme
    {
        if (gx->ExecTS > now) return false;
        gx->CmdReady = false;
        u32 param = gx->CurCmd.Param;
        gx->ExecTS = now;

        //printf("run: %02X %08X\n", gx->CurCmd.Cmd, gx->CurCmd.Param);

        switch(gx->CurCmd.Cmd)
        {
        case GX_MtxMode:
        {
            gx->CurMatrixMode = param & 3;
            break;
        }

        case GX_MtxPush:
        {
            if (gx->CurMatrixMode == Mtx_Pos || gx->CurMatrixMode == Mtx_Vec)
            {
                gx->Status.StackError |= (gx->PosVecMtxStackPtr > 30);

                gx->PosMatrixStack[gx->PosVecMtxStackPtr&0x1F] = gx->PositionMatrix;
                gx->VecMatrixStack[gx->PosVecMtxStackPtr&0x1F] = gx->VectorMatrix;

                gx->PosVecMtxStackPtr = (gx->PosVecMtxStackPtr + 1) & 0x3F;
            }
            else if (gx->CurMatrixMode == Mtx_Proj)
            {
                gx->Status.StackError |= (gx->ProjMtxStackPtr);

                gx->ProjMatrixStack = gx->ProjectionMatrix;

                gx->ProjMtxStackPtr = !gx->ProjMtxStackPtr;
            }
            else // tex matrix
            {
                gx->Status.StackError |= (gx->TexMtxStackPtr);

                gx->TexMatrixStack = gx->TextureMatrix;

                gx->TexMtxStackPtr = !gx->TexMtxStackPtr;
            }
            gx->ExecTS+=16;
            break;
        }

        case GX_MtxPop:
        {
            if (gx->CurMatrixMode == Mtx_Pos || gx->CurMatrixMode == Mtx_Vec)
            {
                // checkme: s6?
                s8 offs = ((s32)param << 26 >> 26);
                gx->PosVecMtxStackPtr = (gx->PosVecMtxStackPtr - offs) & 0x3F;
                gx->Status.StackError |= (gx->PosVecMtxStackPtr > 30);

                gx->PositionMatrix = gx->PosMatrixStack[gx->PosVecMtxStackPtr&0x1F];
                gx->VectorMatrix = gx->VecMatrixStack[gx->PosVecMtxStackPtr&0x1F];
                gx->ClipDirty = true;
                gx->ExecTS+=35;
            }
            else if (gx->CurMatrixMode == Mtx_Proj)
            {
                gx->ProjMtxStackPtr = !gx->ProjMtxStackPtr;
                gx->Status.StackError |= (gx->ProjMtxStackPtr);

                gx->ProjectionMatrix = gx->ProjMatrixStack;
                gx->ClipDirty = true;
                gx->ExecTS+=35;
            }
            else // tex matrix
            {
                gx->TexMtxStackPtr = !gx->TexMtxStackPtr;
                gx->Status.StackError |= (gx->TexMtxStackPtr);

                gx->TextureMatrix = gx->TexMatrixStack;
                gx->ExecTS+=17;
            }
            break;
        }

        case GX_MtxStore:
        {
            if (gx->CurMatrixMode == Mtx_Pos || gx->CurMatrixMode == Mtx_Vec)
            {
                gx->Status.StackError |= ((param & 0x1F) > 30);

                gx->PosMatrixStack[(param & 0x1F)] = gx->PositionMatrix;
                gx->VecMatrixStack[(param & 0x1F)] = gx->VectorMatrix;
            }
            else if (gx->CurMatrixMode == Mtx_Proj)
            {
                gx->ProjMatrixStack = gx->ProjectionMatrix;
            }
            else // tex matrix
            {
                gx->TexMatrixStack = gx->TextureMatrix;
            }
            gx->ExecTS+=16;
            break;
        }

        case GX_MtxRestore:
        {
            if (gx->CurMatrixMode == Mtx_Pos || gx->CurMatrixMode == Mtx_Vec)
            {
                gx->Status.StackError |= ((param & 0x1F) > 30);

                gx->PositionMatrix = gx->PosMatrixStack[(param & 0x1F)];
                gx->VectorMatrix = gx->VecMatrixStack[(param & 0x1F)];
                gx->ClipDirty = true;
                gx->ExecTS+=35;
            }
            else if (gx->CurMatrixMode == Mtx_Proj)
            {
                gx->ProjectionMatrix = gx->ProjMatrixStack;
                gx->ClipDirty = true;
                gx->ExecTS+=35;
            }
            else // tex matrix
            {
                gx->TextureMatrix = gx->TexMatrixStack;
                gx->ExecTS+=17;
            }
            break;
        }

        case GX_MtxIdentity:
        {
            gx->Matrices[gx->CurMatrixMode] = IdentityMatrix;
            if (gx->CurMatrixMode == Mtx_Vec)
            {
                gx->Matrices[Mtx_Pos] = IdentityMatrix;
            }
            if (gx->CurMatrixMode < Mtx_Tex)
            {
                gx->ClipDirty = true;
                gx->ExecTS+=18;
            }
            break;
        }

        case GX_MtxLoad4x4:
        {
            gx->TempMatrix.Arr[gx->TempMtxPtr] = (s64)(s32)param;
            // CHECKME: how does this ptr actually work?
            gx->TempMtxPtr++;
            if (gx->TempMtxPtr == 16)
            {
                gx->TempMtxPtr = 0;
                gx->Matrices[gx->CurMatrixMode] = gx->TempMatrix;

                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    gx->Matrices[Mtx_Pos] = gx->TempMatrix;
                }
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=18;
                }
                else gx->ExecTS+=10;
            }
            break;
        }

        case GX_MtxLoad4x3:
        {
            gx->TempMatrix.Arr[gx->TempMtxPtr] = (s64)(s32)param;
            // CHECKME: how does this ptr actually work?
            gx->TempMtxPtr++;
            if ((gx->TempMtxPtr % 4) == 3) gx->TempMtxPtr++;
            if (gx->TempMtxPtr == 16)
            {
                gx->TempMatrix.Arr[3] = IdentityMatrix.Arr[3];
                gx->TempMatrix.Arr[7] = IdentityMatrix.Arr[7];
                gx->TempMatrix.Arr[11] = IdentityMatrix.Arr[11];
                gx->TempMatrix.Arr[15] = IdentityMatrix.Arr[15];
                gx->TempMtxPtr = 0;
                gx->Matrices[gx->CurMatrixMode] = gx->TempMatrix;

                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    gx->Matrices[Mtx_Pos] = gx->TempMatrix;
                }
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=18;
                }
                else gx->ExecTS+=7;
            }
            break;
        }

        case GX_MtxMul4x4:
        {
            gx->TempMatrix.Arr[gx->TempMtxPtr] = (s64)(s32)param;
            // CHECKME: how does this ptr actually work?
            gx->TempMtxPtr++;
            if (gx->TempMtxPtr == 16)
            {
                gx->TempMtxPtr = 0;
                gx->Matrices[gx->CurMatrixMode] = GX_MatrixMultiply(gx->Matrices[gx->CurMatrixMode], gx->TempMatrix);

                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    gx->Matrices[Mtx_Pos] = GX_MatrixMultiply(gx->Matrices[Mtx_Pos], gx->TempMatrix);
                    gx->ExecTS+=30;
                }
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=35-16;
                }
                else gx->ExecTS+=33-16;
            }
            break;
        }

        case GX_MtxMul4x3:
        {
            gx->TempMatrix.Arr[gx->TempMtxPtr] = (s64)(s32)param;
            // CHECKME: how does this ptr actually work?
            gx->TempMtxPtr++;
            if ((gx->TempMtxPtr % 4) == 3) gx->TempMtxPtr++;
            if (gx->TempMtxPtr == 16)
            {
                gx->TempMatrix.Arr[3] = IdentityMatrix.Arr[3];
                gx->TempMatrix.Arr[7] = IdentityMatrix.Arr[7];
                gx->TempMatrix.Arr[11] = IdentityMatrix.Arr[11];
                gx->TempMatrix.Arr[15] = IdentityMatrix.Arr[15];
                gx->TempMtxPtr = 0;
                gx->Matrices[gx->CurMatrixMode] = GX_MatrixMultiply(gx->Matrices[gx->CurMatrixMode], gx->TempMatrix);

                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    gx->Matrices[Mtx_Pos] = GX_MatrixMultiply(gx->Matrices[Mtx_Pos], gx->TempMatrix);
                    gx->ExecTS+=30;
                }
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=35-12;
                }
                else gx->ExecTS+=33-12;
            }
            break;
        }

        case GX_MtxMul3x3:
        {
            gx->TempMatrix.Arr[gx->TempMtxPtr] = (s64)(s32)param;
            // CHECKME: how does this ptr actually work?
            gx->TempMtxPtr++;
            if ((gx->TempMtxPtr % 4) == 3) gx->TempMtxPtr++;
            if (gx->TempMtxPtr == 12)
            {
                gx->TempMatrix.Arr[3] = IdentityMatrix.Arr[3];
                gx->TempMatrix.Arr[7] = IdentityMatrix.Arr[7];
                gx->TempMatrix.Arr[11] = IdentityMatrix.Arr[11];
                gx->TempMatrix.Row[3] = IdentityMatrix.Row[3];
                gx->TempMtxPtr = 0;
                gx->Matrices[gx->CurMatrixMode] = GX_MatrixMultiply(gx->Matrices[gx->CurMatrixMode], gx->TempMatrix);

                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    gx->Matrices[Mtx_Pos] = GX_MatrixMultiply(gx->Matrices[Mtx_Pos], gx->TempMatrix);
                    gx->ExecTS+=30;
                }
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=35-9;
                }
                else gx->ExecTS+=33-9;
            }
            break;
        }

        case GX_MtxScale:
        {
            gx->ScaleTransVec.Arr[gx->ScaleTransPtr] = (s64)(s32)param;
            gx->ScaleTransPtr++;
            if (gx->ScaleTransPtr == 3)
            {
                gx->ScaleTransPtr = 0;
                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    GX_MatrixScale(&gx->Matrices[Mtx_Pos], gx->ScaleTransVec); // checkme
                }
                else GX_MatrixScale(&gx->Matrices[gx->CurMatrixMode], gx->ScaleTransVec);
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=35-3;
                }
                else gx->ExecTS+=33-3;
            }
            break;
        }

        case GX_MtxTrans:
        {
            gx->ScaleTransVec.Arr[gx->ScaleTransPtr] = (s64)(s32)param;
            gx->ScaleTransPtr++;
            if (gx->ScaleTransPtr == 3)
            {
                gx->ScaleTransPtr = 0;
                GX_MatrixTranslate(&gx->Matrices[gx->CurMatrixMode], gx->ScaleTransVec);
                if (gx->CurMatrixMode == Mtx_Vec)
                {
                    GX_MatrixTranslate(&gx->Matrices[Mtx_Pos], gx->ScaleTransVec);
                }
                if (gx->CurMatrixMode < Mtx_Tex)
                {
                    gx->ClipDirty = true;
                    gx->ExecTS+=35-3;
                }
                else gx->ExecTS+=33-3;
            }
            break;
        }

        case GX_VtxColor:
        {
            gx->VertexColor.R = (param >>  0) & 0x1F;
            gx->VertexColor.G = (param >>  5) & 0x1F;
            gx->VertexColor.B = (param >> 10) & 0x1F;
            break;
        }

        case GX_LgtNormal:
        case GX_TstVec:
        {
            GX_UpdateNormal(sys, param, gx->CurCmd.Cmd == GX_TstVec);
            break;
        }

        case GX_TexCoord:
        {
            gx->TransTexCoords[0] = gx->TexCoords[0] = param & 0xFFFF;
            gx->TransTexCoords[1] = gx->TexCoords[1] = param >> 16;
            if (gx->TexAttr.CoordTransMode == 1)
            {
                gx->TransTexCoords[0] = ((gx->TexCoords[0] * gx->TextureMatrix.Arr[0]) + (gx->TexCoords[1] * gx->TextureMatrix.Arr[4]) + gx->TextureMatrix.Arr[8] + gx->TextureMatrix.Arr[12]) >> 12;
                gx->TransTexCoords[1] = ((gx->TexCoords[0] * gx->TextureMatrix.Arr[1]) + (gx->TexCoords[1] * gx->TextureMatrix.Arr[5]) + gx->TextureMatrix.Arr[9] + gx->TextureMatrix.Arr[13]) >> 12;
            }
            break;
        }

        case GX_Vtx16:
        case GX_TstPos:
        {
            if (!gx->TmpVertexPtr)
            {
                gx->TmpVertex.X = (s64)(s16)param;
                gx->TmpVertex.Y = (s64)(s16)(param >> 16);
            }
            else
            {
                gx->TmpVertex.Z = (s64)(s16)param;
                GX_SubmitVertex(sys, nullptr, gx->CurCmd.Cmd == GX_TstPos);
            }

            gx->TmpVertexPtr = !gx->TmpVertexPtr;
            break;
        }

        case GX_Vtx10:
        {
            gx->TmpVertex.Vec = (Vec){(s64)(s16)(param << 6), (s64)(s16)((param >> 10) << 6), (s64)(s16)((param >> 20) << 6), (1<<12)};
            GX_SubmitVertex(sys, nullptr, false);
            break;
        }

        case GX_VtxXY:
        {
            gx->TmpVertex.X = (s64)(s16)param;
            gx->TmpVertex.Y = (s64)(s16)(param >> 16);
            GX_SubmitVertex(sys, nullptr, false);
            break;
        }

        case GX_VtxXZ:
        {
            gx->TmpVertex.X = (s64)(s16)param;
            gx->TmpVertex.Z = (s64)(s16)(param >> 16);
            GX_SubmitVertex(sys, nullptr, false);
            break;
        }

        case GX_VtxYZ:
        {
            gx->TmpVertex.Y = (s64)(s16)param;
            gx->TmpVertex.Z = (s64)(s16)(param >> 16);
            GX_SubmitVertex(sys, nullptr, false);
            break;
        }

        case GX_VtxDiff:
        {
            gx->TmpVertex.Vec += (Vec){(s64)((s16)(param << 6) >> 6), (s64)((s16)((param >> 10) << 6) >> 6), (s64)((s16)((param >> 20) << 6) >> 6), 0};
            gx->TmpVertex.Vec = (gx->TmpVertex.Vec << 48) >> 48;
            GX_SubmitVertex(sys, nullptr, false);
            break;
        }

        case GX_PlyAttr:
        {
            gx->NextPolyAttr.Raw = param;
            break;
        }

        case GX_TexAttr:
        {
            // CHECKME: do these do anything weird like polygon attributes?
            gx->TexAttr.Raw = param;
            break;
        }

        case GX_TexPal:
        {
            // CHECKME: do these do anything weird like polygon attributes?
            gx->TexPal = param;
            break;
        }

        case GX_LgtDifAmb:
        {
            gx->DiffColor.R = (param >>  0) & 0x1F;
            gx->DiffColor.G = (param >>  5) & 0x1F;
            gx->DiffColor.B = (param >> 10) & 0x1F;

            gx->AmbiColor.R = (param >> 16) & 0x1F;
            gx->AmbiColor.G = (param >> 21) & 0x1F;
            gx->AmbiColor.B = (param >> 26) & 0x1F;

            if (param & (1<<15)) gx->VertexColor = gx->DiffColor;
            gx->ExecTS += 3;
            break;
        }

        case GX_LgtSpeEmi:
        {
            gx->SpecColor.R = (param >>  0) & 0x1F;
            gx->SpecColor.G = (param >>  5) & 0x1F;
            gx->SpecColor.B = (param >> 10) & 0x1F;

            gx->EmisColor.R = (param >> 16) & 0x1F;
            gx->EmisColor.G = (param >> 21) & 0x1F;
            gx->EmisColor.B = (param >> 26) & 0x1F;

            gx->UseSpecTable = (param & (1<<15));
            gx->ExecTS += 3;
            break;
        }

        case GX_LgtVector:
        {
            // s1.9
            s64 dir[3] = {(s64)((s32)(param >>  0) << 22) >> 22,
                          (s64)((s32)(param >> 10) << 22) >> 22,
                          (s64)((s32)(param >> 20) << 22) >> 22};

            gx->LightVec[param>>30].Vec = (dir[0] * gx->VectorMatrix.Row[0]) + (dir[1] * gx->VectorMatrix.Row[1]) + (dir[2] * gx->VectorMatrix.Row[2]);

            // calculate reciprocal to use for specular lighting
            // Note: convert to s1.10 -> negate; this is different from the actual vector calc for some reason?
            s32 den = -((gx->LightVec[param>>30].Z << 41) >> 53) + (1<<9);
            gx->LightRecip[param>>30] = ((den == 0) ? 0 : ((1<<18) / den));

            // Note: negate -> convert to s1.10; this is different than the reciprocal calc for some reason?
            gx->LightVec[param>>30].Vec = (-(gx->LightVec[param>>30].Vec>>12) << 53) >> 53;
            gx->ExecTS += 5;
            break;
        }

        case GX_LgtColor:
        {
            // TODO: 8 cycle delay after vertex command
            gx->LightColor[param>>30].R = (param >>  0) & 0x1F;
            gx->LightColor[param>>30].G = (param >>  5) & 0x1F;
            gx->LightColor[param>>30].B = (param >> 10) & 0x1F;
            gx->ExecTS += 3;
            break;
        }

        case GX_LgtSpecTable:
        {
            gx->SpecTableb32[gx->SpecTablePtr] = param;
            gx->SpecTablePtr = (gx->SpecTablePtr + 1) % (128/sizeof(u32));
            break;
        }

        case GX_PlyBegin:
        {
            GX_PolygonBegin(gx, param & 0x3);
            break;
        }

        case GX_PlyEnd:
        {
            // shaddup
            break;
        }

        case GX_SwapBuffers:
        {
            gx->ManualTransSort = param & 1;
            gx->WBuffer = param & 2;
            gx->ExecTS = timestamp_max;
            if (!gx->PartialPolygon)
            {
                gx->SwapReq = true;
            }
            else LogPrint(LOG_GX|LOG_EXCEP, "GX HANGING, UH OH\n");
            break;
        }

        case GX_Viewport:
        {
            gx->ViewportLeft = param & 0xFF;
            gx->ViewportTop = 191-((param>>24) & 0xFF);
            gx->ViewportWidth = (((param>>16) & 0xFF) - gx->ViewportLeft + 1) & 0x1FF; // x coord is u9
            gx->ViewportHeight = (((191 - (param >> 8)) & 0xFF) - gx->ViewportTop + 1) & 0xFF; // y coord is u8
            break;
        }

        case GX_TstBox:
        {
            gx->BoxTestParams[(gx->BoxTestPtr*2)+0] = param & 0xFFFF;
            gx->BoxTestParams[(gx->BoxTestPtr*2)+1] = (param >> 16) & 0xFFFF;
            gx->BoxTestPtr = (gx->BoxTestPtr + 1) % 3;
            if (gx->BoxTestPtr == 0)
            {
                GX_BoxTest(sys);
            }
            // TODO: timings, busy flag
            break;
        }

        case 0x00: // nop
            break;
        default:
        {
            LogPrint(LOG_GX|LOG_UNIMP, "Unimplemented GX Command: %02X %08X\n", gx->CurCmd.Cmd, gx->CurCmd.Param);
            break;
        }
        }
        return true; // TODO/CHECKME?
    }
    else
    {
        bool suc = GX_FetchParams(sys);
        gx->CmdReady = suc;
        return suc;
    }
}

int GX_Cmp(const void* a, const void* b)
{
    return ((Polygon*)a)->SortKey - ((Polygon*)b)->SortKey;
}

void GX_Swap(struct Console* sys, const timestamp now)
{
    GX3D* gx = &sys->GX3D;

    if (gx->SwapReq)
    {
        gx->ExecTS = now + 325;
        gx->SwapReq = false;

        // sort polygon ram
        qsort(gx->GXPolyRAM, gx->PolyRAMPtr, sizeof(gx->PolyRAMA[0]), GX_Cmp);

        gx->VtxRAMPtr = 0;
        gx->RenderPolyCount = gx->PolyRAMPtr;
        gx->PolyRAMPtr = 0;

        DS_SWAP(gx->GXPolyRAM, gx->RenderPolyRAM);

        DS_SWAP(gx->GXVtxRAM, gx->RenderVtxRAM);

        gx->LatRasterCR = gx->RasterCR;
        gx->LatRearAttr = gx->RearAttr;
        gx->LatRearDepth = gx->RearDepth;
        gx->RenderWBuffer = gx->WBuffer;

        // make sure to reschedule if needed, since we probably ended up getting this scheduled 5 years into the future, and that might cause problems.
        if (sys->Sched.EventTimes[Evt_GX] > gx->ExecTS)
            Schedule_Event(sys, GX_RunFIFO, Evt_GX, gx->ExecTS);
    }
}
