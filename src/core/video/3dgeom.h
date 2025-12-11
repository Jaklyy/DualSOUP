#pragma once
#include "../utils.h"




enum GXCmds
{
    GX_MtxMode = 0x10,
    GX_MtxPush,
    GX_MtxPop,
    GX_MtxStore,
    GX_MtxRestore,
    GX_MtxIdentity,
    GX_MtxLoad4x4,
    GX_MtxLoad4x3,
    GX_MtxMul4x4,
    GX_MtxMul4x3,
    GX_MtxMul3x3,
    GX_MtxScale,
    GX_MtxTrans,

    GX_VtxColor = 0x20,
    GX_LgtNormal,
    GX_TexCoord,
    GX_Vtx16,
    GX_Vtx10,
    GX_VtxXY,
    GX_VtxXZ,
    GX_VtxYZ,
    GX_VtxDiff,
    GX_PlyAttr,
    GX_TexAttr,
    GX_TexPal,

    GX_LgtDifAmb = 0x30,
    GX_LgtSpeEmi,
    GX_LgtVector,
    GX_LgtColor,
    GX_LgtShineTable,

    GX_VtxBegin = 0x40,
    GX_VtxEnd,

    GX_SwapBuffers = 0x50,

    GX_Viewport = 0x60,

    GX_TstBox = 0x70,
    GX_TstPos,
    GX_TstVec,
};

constexpr s8 ParamLUT[256] =
{
    [GX_MtxMode] = 1,
    [GX_MtxPush] = 0,
    [GX_MtxPop] = 1,
    [GX_MtxStore] = 1,
    [GX_MtxRestore] = 1,
    [GX_MtxIdentity] = 0,
    [GX_MtxLoad4x4] = 16,
    [GX_MtxLoad4x3] = 12,
    [GX_MtxMul4x4] = 16,
    [GX_MtxMul4x3] = 12,
    [GX_MtxMul3x3] = 9,
    [GX_MtxScale] = 3,
    [GX_MtxTrans] = 3,

    [GX_VtxColor] = 1,
    [GX_LgtNormal] = 1,
    [GX_TexCoord] = 1,
    [GX_Vtx16] = 2,
    [GX_Vtx10] = 1,
    [GX_VtxXY] = 1,
    [GX_VtxXZ] = 1,
    [GX_VtxYZ] = 1,
    [GX_VtxDiff] = 1,
    [GX_PlyAttr] = 1,
    [GX_TexAttr] = 1,
    [GX_TexPal] = 1,

    [GX_LgtDifAmb] = 1,
    [GX_LgtSpeEmi] = 1,
    [GX_LgtVector] = 1,
    [GX_LgtColor] = 1,
    [GX_LgtShineTable] = 32,

    [GX_VtxBegin] = 1,
    [GX_VtxEnd] = 0,

    [GX_SwapBuffers] = 1,
    [GX_Viewport] = 1,

    [GX_TstBox] = 3,
    [GX_TstPos] = 2,
    [GX_TstVec] = 1
};

typedef struct
{
    alignas(u64)
    u32 Param;
    u8 Cmd;
} GXCmd;

typedef struct
{
    GXCmd CurCmd;
    GXCmd Pipe[4];
    GXCmd FIFO[256];

    timestamp Timestamp;

    union
    {
        u8 CurCmd;
        u32 All;
    } PackBuffer; // I had the initial thought that the pipe might fill this role on hw? but it seems to be completely different actually.
    bool FreshBuffer;
    s8 ParamRem;
    bool BufferFree;

    u16 PipeWrPtr;
    u8 PipeRdPtr;
    u8 FIFOWrPtr;
    u8 FIFORdPtr;
    bool CmdBusy;

    u16 FIFOFullness;

    union
    {
        u32 Raw;
        struct
        {
            bool TestBusy : 1;
            bool BoxTestRes : 1;
            u32 : 6;
            u32 PosVecStackPtr : 5;
            u32 ProjStackPtr : 1;
            bool PushPopBusy : 1;
            bool StacKError : 1;
            u32 NumCmdsInFIFO : 9;
            bool FIFOHalfEmpty : 1;
            bool FIFOEmpty : 1;
            bool GXBusy : 1;
            u32 : 2;
            u32 FIFOIRQMode : 2;
        };
    } Status;
} GX3D;

struct Console;

void GX_IOWrite(struct Console* sys, const u32 addr, const u32 mask, const u32 val);
u32 GX_IORead(struct Console* sys, const u32 addr);
