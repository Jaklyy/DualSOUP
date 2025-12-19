#pragma once
#include "../utils.h"




constexpr unsigned TileWidth = 8;
constexpr unsigned TileHeight = 8;
constexpr unsigned TileSize = TileWidth*TileHeight;
constexpr unsigned BytesPerTile = 2;
constexpr unsigned ScreenWidthTiles = 32;
constexpr unsigned ScreenHeightTiles = 32;
constexpr unsigned ScreenSizeTiles = ScreenWidthTiles * ScreenHeightTiles;
constexpr unsigned ScreenWidthPx = TileWidth * ScreenWidthTiles;
constexpr unsigned ScreenHeightPx = TileHeight * ScreenHeightTiles;
constexpr unsigned ScreenSizePx = ScreenWidthPx * ScreenHeightPx;

typedef union
{
    u16 Raw;
    struct
    {
        u16 TileNum : 10;
        bool HFlip : 1;
        bool VFlip : 1;
        u16 Palette : 4;
    };
} TextTileData;

typedef struct
{
    u32 Index : 24;
    u32 SprBG : 2;
    bool Empty : 1;
    bool NotPal : 1;
    bool ExtPal : 1;
    bool GPU3D : 1;
} CompositeBuffer;

typedef union
{
    u32 Raw;
    struct
    {
        u32 Y : 8;
        bool RotScal : 1;
        bool Disable : 1; // normal spr
        u32 Mode : 2;
        bool Mosaic : 1;
        bool Pal256 : 1;
        u32 Shape : 2;
        s32 X : 9;
        u32 : 3;
        bool HFlip : 1;
        bool VFlip : 1;
        u32 Size : 2;
    };
    struct
    {
        u32 : 9;
        bool DoubleSize : 1; // rotscale
        u32 : 15;
        u32 RotScaleParam : 5;
    };
} SprAttrs01;

typedef union
{
    u16 Raw;
    struct
    {
        u32 TileNum : 10;
        u32 BGPriority : 2;
        u32 PaletteOffset : 4;
    };
} SprAttrs2;

typedef struct
{
    union
    {
        u32 Raw;
        struct
        {
            u32 BGSetup : 3;
            bool BG03D : 1;
            bool TileOBJ1D : 1;
            bool BitmapOBJ2DDims : 1; // clear == 128x512; set == 256x256
            bool BitmapOBJ1D : 1;
            bool ForceBlank : 1;
            bool BG0Enable : 1;
            bool BG1Enable : 1;
            bool BG2Enable : 1;
            bool BG3Enable : 1;
            bool SprEnable : 1;
            bool Win0Enable : 1;
            bool Win1Enable : 1;
            bool OBJWinEnable : 1;
            u32 DisplayMode : 2;
            u32 VRAMSel : 2;
            u32 TileOBJ1DBound : 2;
            bool BMPOBJ1DBound : 1;
            bool OBJHBlankDisable : 1;
            u32 CharBase : 3;
            u32 ScreenBase : 3;
            bool BGExtPalEn : 1;
            bool SprExtPalEn : 1;
        };
    } DisplayCR;

    union
    {
        u16 Raw;
        struct
        {
            u16 BGPriority : 2;
            u16 CharBase : 4;
            bool Mosaic : 1;
            bool Pal256 : 1;
            u16 ScreenBase : 5;
            bool ExtPalSlot : 1; // bg0/bg1
            u16 ScreenSize : 2;
        };
        struct
        {
            u16 : 14;
            bool Wide : 1;
            bool Tall : 1;
        };
        struct
        {
            u16 : 13;
            bool OverflowWrap : 1; 
        };
    } BGCR[4];

    u16 Xoff[4];
    u16 Yoff[4];
} PPU;

struct Console;
void PPU_RenderScanline(struct Console* sys, const bool b, const s16 y);
int PPUA_MainLoop(void* ptr);
int PPUB_MainLoop(void* ptr);