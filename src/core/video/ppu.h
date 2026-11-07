#pragma once
#include <SDL3/SDL_thread.h>
#include "core/utils.h"




constexpr u32 BGTileWidth = 8;
constexpr u32 BGTileHeight = 8;
constexpr u32 BGTileSize = BGTileWidth*BGTileHeight;
constexpr u32 BGBytesPerTile = 2;
constexpr u32 BGScreenWidthTiles = 32;
constexpr u32 BGScreenHeightTiles = 32;
constexpr u32 BGScreenSizeTiles = BGScreenWidthTiles * BGScreenHeightTiles;
constexpr u32 BGScreenWidthPx = BGTileWidth * BGScreenWidthTiles;
constexpr u32 BGScreenHeightPx = BGTileHeight * BGScreenHeightTiles;
constexpr u32 BGScreenSizePx = BGScreenWidthPx * BGScreenHeightPx;
constexpr u32 BGScreenWidthBytes = BGScreenWidthTiles * BGBytesPerTile;
constexpr u32 BGScreenSizeBytes = BGScreenSizeTiles * BGBytesPerTile;
constexpr u32 AGB_LCDWidth = 240;
constexpr u32 NTR_LCDWidth = 256;


#define LCD_GetWidth(AGB) ((AGB) ? 240 : 256)
#define LCD_GetHeight(AGB) ((AGB) ? 160 : 192)
#define LCD_GetVCountMax(AGB) ((AGB) ? 227 : 262)
#define PPU_GetIterCycles(AGB) ((AGB) ? 4 : 6)
#define LCD_GetHBlankStart(AGB) ((AGB) ? AGBActiveRender_Cycles : NTRActiveRender_Cycles)

#define LCD_Width LCD_GetWidth(type==PPUType_AGB)
#define LCD_Height LCD_GetHeight(type==PPUType_AGB)
#define LCD_VMax LCD_GetVCountMax(type==PPUType_AGB)
#define PPU_IterLen PPU_GetIterCycles(type==PPUType_AGB)
#define LCD_HBlankStart LCD_GetHBlankStart(type==PPUType_AGB)

typedef enum : u8
{
    PPUType_A,
    PPUType_B,
    PPUType_AGB,
} PPU_Type;

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
    bool Mosaic : 1;
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

constexpr u32 DispCRWrMasks[] = {0xFFFFFFFF /* PPU/LCDC A */, 0xC0B1FFF7 /* PPU/LCDC B */, 0x0001FFF7 /* PPU AGB MODE */};
constexpr u16 BGOffsetWrMask = 0x1FF;

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
constexpr u32 WinCRWrMasks[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0x3F3F3F3F};

typedef enum
{
    BLDCR_Off,
    BLDCR_Blend,
    BLDCR_Bright,
    BLDCR_Dark,
} BlendMode;

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
        u16 Effect : 2;
        bool BG0S : 1;
        bool BG1S : 1;
        bool BG2S : 1;
        bool BG3S : 1;
        bool SprS : 1;
        bool BDrS : 1;
    };
    struct
    {
        u16 BlendTop : 6;
        u16 : 2;
        u16 BlendBot : 6;
    };
} BlendCR;
constexpr u16 BlendCRWrMask = 0x3FFF;
constexpr u8 BlendParamWrMask = 0x1F;

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
constexpr u16 LCDCBrightnessWrMask = 0xC01F;

typedef union
{
    u16 Raw;
    struct
    {
        u16 BgX : 4;
        u16 BgY : 4;
        u16 SprX : 4;
        u16 SprY : 4;
    };
} Mosaic;

typedef struct
{
    TextTileData Tile;
    u16 ColorData;
    u16 BgX;
} BGData;

typedef struct
{
    s32 AffineCur[2];
    SprAttrs01 OAM01;
    SprAttrs2 OAM2;
    s16 AffineParams[4];
    u16 ColorData;
    u8 Width;
    u8 WidthReal;
    u8 Height;
    u8 HeightReal;
    u8 SprID;
    u16 Cycle;
} SprData;

typedef struct
{
    alignas(HOST_CACHEALIGN) CompositeBuffer CompositeBuffer[6][256];
    alignas(HOST_CACHEALIGN) u32 OutLine[256];
    alignas(alignof(u64)*4) u64 SpriteWindow[2][4];
    union
    {
        u32 Raw;
        struct
        {
            u32 BGMode : 3;
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
        struct
        {
            u16 : 3;
            bool CGBMode : 1; // switches the ppu into a mode for displaying the cbg graphics output; dunno if this works on nds?
            bool FrameSelect : 1;
            bool OBJHBlankDisable : 1;
            bool OBJ1D : 1;
            u16 : 9;
            bool StereoscopyMode : 1; // supposedly still has partial functionality on NDS/3DS?
        } AGB; // agb different bits
    } DisplayCR;

    union
    {
        u8 Raw;
        struct
        {
            bool ForceBlank : 1;
            bool BG0 : 1;
            bool BG1 : 1;
            bool BG2 : 1;
            bool BG3 : 1;
            bool Spr : 1;
        };
    } DispEnPipe[3];

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
    bool AffineDirty[2];
    s32 AffineCur[2][2];
    s32 AffineStart[2][2];
    s32 AffineCoord[2][2];
    s16 AffineRotParams[2][4];
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
    WindowCR WinEnMask;

    Mosaic MosaicCR;
    u8 SprMosaicYCur;
    u8 SprMosaicYFrac;

    u8 SprMosaicXCur;
    u8 SprMosaicXFrac;

    BGData BGDat[4];
    timestamp ScanlineStart;
    s8 CompBGs[2];
    CompositeBuffer CompSel[2];
    u32 CompCol[2];
    bool SprBufBack;
    SprData SprDat[2];
    bool SprDatCur;
} PPU;

typedef struct Console Console;
void PPU_RenderScanline(Console* sys, const bool b, const s16 y);
void PPU_GlobalStep(Console* sys, const timestamp now, const u16 vcount);

void PPU_IORead(PPU* ppu, u32* rdata, const u32 addr);
void PPU_IOWrite(PPU* ppu, const u32 addr, const u32 val, u32 mask, const bool b, const bool ppuenable, const bool agb);

int SDLCALL PPUA_MainLoop(void* ptr);
int SDLCALL PPUB_MainLoop(void* ptr);
