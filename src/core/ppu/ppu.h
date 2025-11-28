#pragma once
#include "../utils.h"



struct PPU
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
            bool OBJEnable : 1;
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
            bool OBJExtPalEn : 1;
        };
    } DisplayCR;
};

