#pragma once
#include "../utils.h"




enum GXCmds : u8
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
    GX_LgtSpecTable,

    GX_PlyBegin = 0x40,
    GX_PlyEnd,

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
    [GX_LgtSpecTable] = 32,

    [GX_PlyBegin] = 1,
    [GX_PlyEnd] = 0,

    [GX_SwapBuffers] = 1,
    [GX_Viewport] = 1,

    [GX_TstBox] = 3,
    [GX_TstPos] = 2,
    [GX_TstVec] = 1
};

enum Matrices
{
    Mtx_Proj,
    Mtx_Pos,
    Mtx_Vec,
    Mtx_Tex,
};

enum PolyTypes
{
    Poly_Tri,
    Poly_Quad,
    Poly_TriStrip,
    Poly_QuadStrip,
};

typedef s64 Vec __attribute__ ((vector_size(sizeof(s64)*4)));
typedef s64 Mtx __attribute__ ((vector_size(sizeof(s64)*16)));

typedef struct
{
    alignas(u64)
    u32 Param;
    u8 Cmd;
} GXCmd;

typedef union
{
    alignas(4)
    u32 Arr[3];
    u32 RGB __attribute__ ((vector_size(sizeof(u32)*4)));
    struct
    {
        u32 R;
        u32 G;
        u32 B;
    };
} Colors;

typedef union
{
    s64 Arr[4];
    struct
    {
        s64 X;
        s64 Y;
        s64 Z;
        s64 W;
    };
    Vec Vec;
} Vector;

typedef union
{
    s64 Arr[16];
    Vec Row[4];
    Mtx Vec;
} Matrix;

constexpr Matrix IdentityMatrix =
{
    .Arr = {
    1<<12, 0, 0, 0,
    0, 1<<12, 0, 0,
    0, 0, 1<<12, 0,
    0, 0, 0, 1<<12
    }
};

typedef union
{
    u32 Raw;
    struct
    {
        bool Light0 : 1;
        bool Light1 : 1;
        bool Light2 : 1;
        bool Light3 : 1;
        u32 Mode : 2;
        bool RenderBack : 1;
        bool RenderFront : 1;
        u32 : 3;
        bool TranslucentDepthUpdate : 1;
        bool RenderFarPlaneClipped : 1;
        bool RenderZDot : 1;
        bool EqualDepthTest : 1;
        bool FogEnable : 1;
        u32 Alpha : 4;
        u32 : 3;
        u32 PolyID : 6;
    };
    struct
    {
        u32 LightEnables : 4;
    };
} PolyAttr;


typedef struct
{
    u16 X; // u9
    s16 Z; // s16?
    u16 W; // u16?
    u8 Y; // u8
    Colors Color;
} Vertex;

typedef struct
{
    Vertex* Vertices[10];
    bool Frontfacing;
    PolyAttr Attrs;
    u8 NumVert;
    bool Trans;
    u8 VTop;
    u8 VBot;
    u8 Top;
    u8 Bot;
    u32 SortKey;
    int WDecompress;
    u8 SlopeY[10]; // u8; slope start/end y coordinates; this is stored with the polygon data instead of the vertex data for some reason.
} Polygon;

typedef struct 
{
    Vector Coords;
    Vector CoordsInitial;
    Colors Color;
} VertexTmp;

typedef struct
{
    VertexTmp Vertices[10];
    bool Clipped;
} PolygonTmp;

typedef union
{
    u32 Raw;
    struct
    {
        bool TXMajor : 1;
        bool RYMajor : 1;
    };
} AttrBuf;


typedef union
{
    u16 Raw;
    struct
    {
        bool Texture : 1;
        bool ShadingMode : 1;
        bool AlphaTest : 1;
        bool AlphaBlend : 1;
        bool AntiAlias : 1;
        bool EdgeMark : 1;
        bool FogMode : 1;
        bool Fog : 1;
        u16 FogShift : 4;
        bool ScanlineUnderrun : 1;
        bool VtxPolyRAMLimit : 1;
        bool BitmapRearPlane : 1;
    };
} RasterCR; // 3d display control

typedef union
{
    u32 Raw;
    struct
    {
        u32 R : 5;
        u32 G : 5;
        u32 B : 5;
        bool Fog : 1;
        u32 Alpha : 5;
        u32 : 3;
        u32 ID : 6;
    };
} RearAttr;




typedef struct
{
    GXCmd CurCmd;
    GXCmd Pipe[4];
    GXCmd FIFO[256];

    timestamp Timestamp;
    timestamp ExecTS;

    union
    {
        u8 CurCmd;
        u32 All;
    } PackBuffer; // I had the initial thought that the pipe might fill this role on hw? but it seems to be completely different actually.
    bool FreshBuffer;
    bool BufferFree;
    s8 ParamRem;

    u16 PipeWrPtr;
    u8 PipeRdPtr;

    u8 FIFOWrPtr;
    u8 FIFORdPtr;

    u16 FIFOFullness;

    bool CmdBusy;
    bool CmdReady;

    Colors LightColor[4];
    Colors DiffColor;
    Colors AmbiColor;
    Colors SpecColor;
    Colors EmisColor;
    Colors VertexColor;
    Vector LightVec[4];
    s32 LightRecip[4];
    bool UseSpecTable;

    union
    {
        struct
        {
            Matrix ProjectionMatrix;
            Matrix PositionMatrix;
            Matrix VectorMatrix;
            Matrix TextureMatrix;
        };
        Matrix Matrices[4];
    };

    Matrix ClipMatrix;

    Matrix ProjMatrixStack;
    Matrix VecMatrixStack[32];
    Matrix PosMatrixStack[32];
    Matrix TexMatrixStack; // checkme

    Matrix TempMatrix;
    u8 TempMtxPtr;
    Vector ScaleTransVec;
    u8 ScaleTransPtr;
    bool TmpVertexPtr;
    Vector TmpVertex;

    u8 CurMatrixMode;
    bool ProjMtxStackPtr;
    bool TexMtxStackPtr; // checkme?
    u8 PosVecMtxStackPtr;
    bool ClipDirty;

    union
    {
        u32 SpecTableb32[128/sizeof(u32)];
        u8 SpecTable[128];
    };
    u8 SpecTablePtr;

    u8 PolygonType;
    u8 TmpPolygonPtr;
    bool PartialPolygon;
    bool TriStripOdd;
    PolygonTmp PolygonTmp;

    Vertex VtxRAMA[6144];
    Polygon PolyRAMA[2048];
    Vertex VtxRAMB[6144];
    Polygon PolyRAMB[2048];
    u16 PolyRAMPtr;
    u16 VtxRAMPtr;
    Vertex* GXVtxRAM;
    Polygon* GXPolyRAM;

    Vertex* SharedVtx[2];

    u8 ViewportLeft;
    u8 ViewportTop;
    u8 ViewportHeight;
    u16 ViewportWidth;

    PolyAttr NextPolyAttr;
    PolyAttr CurPolyAttr;

    bool ManualTransSort;
    bool WBuffer;

    bool SwapReq;

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
            bool StackError : 1;
            u32 NumCmdsInFIFO : 9;
            bool FIFOHalfEmpty : 1;
            bool FIFOEmpty : 1;
            bool GXBusy : 1;
            u32 : 2;
            u32 FIFOIRQMode : 2;
        };
    } Status;

    RasterCR RasterCR;
    RearAttr RearAttr;
    u16 RearDepth;



    alignas(HOST_CACHEALIGN)
    Vertex* RenderVtxRAM;
    Polygon* RenderPolyRAM;
    u16 RenderPolyCount;
    bool RenderWBuffer;
    RasterCR LatRasterCR;
    RearAttr LatRearAttr;
    u16 LatRearDepth;

    alignas(HOST_CACHEALIGN)
    u32 CBuf[2][192][256];
    u32 ZBuf[2][192][256];
    AttrBuf ABuf[2][192][256];
} GX3D;

struct Console;

bool GX_FetchParams(struct Console* sys);
bool GX_RunCommand(struct Console* sys, const timestamp now);
void GX_Swap(struct Console* sys, const timestamp now);
void GX_RunFIFO(struct Console* sys, const timestamp until);
void SWRen_RasterizerFrame(struct Console* sys);

void GX_IOWrite(struct Console* sys, const u32 addr, const u32 mask, const u32 val);
u32 GX_IORead(struct Console* sys, const u32 addr);
