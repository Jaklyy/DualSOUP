#pragma once
#include <SDL3/SDL_thread.h>
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
    u32 SprPrio : 2;
    bool Empty : 1;
    bool NotPal : 1;
    bool ExtPal : 1;
    bool ForceBlend : 1;
    bool HasAlpha : 1;
} CompositeBuffer;

typedef union
{
    u32 Raw;
    struct
    {
        u32 Y : 8;
        bool Affine : 1;
        bool Disable : 1; // enables double size for affine sprites
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
        bool DoubleSize : 1; // disables sprite if affine bit isn't set
        u32 : 15;
        u32 AffineParam : 5;
    };
} SprAttrs01;

typedef union
{
    u16 Raw;
    struct
    {
        u16 TileNum : 10;
        u16 Priority : 2;
        u16 PaletteOffset : 4;
    };
    struct
    {
        u16 : 12;
        u16 BitmapAlpha : 4;
    };
} SprAttrs2;

typedef union
{
    u16 Raw;
    struct
    {
        u16 Factor : 5;
        u16 : 9;
        u16 Mode : 2;
    };
} Brightness;

typedef union
{
    u16 Raw;
    struct
    {
        bool BG0F : 1;
        bool BG1F : 1;
        bool BG2F : 1;
        bool BG3F : 1;
        bool SprF : 1;
        bool BDrF : 1;
        u32 Effect : 2;
        bool BG0S : 1;
        bool BG1S : 1;
        bool BG2S : 1;
        bool BG3S : 1;
        bool SprS : 1;
        bool BDrS : 1;
    };
    struct
    {
        u32 BlendTop : 6;
        u32 : 2;
        u32 BlendBot : 6;
    };
} BlendCR;

typedef union
{
    u8 Raw;
    struct
    {
        bool EnableBg0 : 1;
        bool EnableBg1 : 1;
        bool EnableBg2 : 1;
        bool EnableBg3 : 1;
        bool EnableObj : 1;
        bool EnableBlend : 1;
    };
} WindowCR;

typedef enum
{
    BLDCR_Off,
    BLDCR_Blend,
    BLDCR_Bright,
    BLDCR_Dark,
} BlendMode;

typedef struct
{
    alignas(HOST_CACHEALIGN) CompositeBuffer CompositeBuffer[5][256];
    alignas(alignof(u64)*4) u64 SpriteWindow[4];
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
            bool BitmapOBJ1DBound : 1;
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
    Brightness Brightness;
    BlendCR BlendCR;
    u8 BlendAlpha[2];
    u8 BlendBright;
    bool Window0YActive;
    bool Window0XActive;
    bool Window1YActive;
    bool Window1XActive;
    union
    {
        u32 Raw[3];
        struct
        {
            u8 W0Right;
            u8 W0Left;

            u8 W1Right;
            u8 W1Left;

            u8 W0Bot;
            u8 W0Top;

            u8 W1Bot;
            u8 W1Top;

            WindowCR W0Cr;
            WindowCR W1Cr;
            WindowCR WNoneCr;
            WindowCR WObjCr;
        };
    } Window;
} PPU;

struct Console;
void PPU_RenderScanline(struct Console* sys, const bool b, const s16 y);
void PPU_GlobalStep(struct Console* sys, const timestamp now, const u16 vcount);

u32 PPU_IORead(PPU* ppu, const u32 addr);
void PPU_IOWrite(PPU* ppu, const u32 addr, const u32 val, u32 mask, const bool PPUEn);

int SDLCALL PPUA_MainLoop(void* ptr);
int SDLCALL PPUB_MainLoop(void* ptr);
