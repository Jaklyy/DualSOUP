#pragma once
#include <SDL3/SDL.h>

#include "imgui/dcimgui.h"

#include "core/console.h"
#include "core/utils.h"




constexpr int GUI_MinDisplayWindows = 1;
constexpr int GUI_MaxDisplayWindows = 4;

constexpr int GUI_MinDisplaysPerWindow = 1;
constexpr int GUI_MaxDisplaysPerWindow = 4;

// TODO: idk what a sane max for screen coordinates actually is, i guess current display resolution...?
constexpr float GUI_MinDisplayWindowWidth = -999999.0;
constexpr float GUI_MaxDisplayWindowWidth =  999999.0;

constexpr float GUI_MinDisplayWindowHeight = -999999.0;
constexpr float GUI_MaxDisplayWindowHeight =  999999.0;

constexpr float GUI_MinDisplayWindowPosX = 0;
constexpr float GUI_MaxDisplayWindowPosX = 999999.0;

constexpr float GUI_MinDisplayWindowPosY = 0;
constexpr float GUI_MaxDisplayWindowPosY = 999999.0;

constexpr float GUI_MinDisplayWidth = -999999.0;
constexpr float GUI_MaxDisplayWidth =  999999.0;

constexpr float GUI_MinDisplayHeight = -999999.0;
constexpr float GUI_MaxDisplayHeight =  999999.0;

constexpr float GUI_MinDisplayPosX = 0;
constexpr float GUI_MaxDisplayPosX = 999999.0;

constexpr float GUI_MinDisplayPosY = 0;
constexpr float GUI_MaxDisplayPosY = 999999.0;

constexpr int GUI_LibMax = 1000;

#define GUI_INPUTCLAMPED(type, label, var, rangelo, rangehi) \
    if (ImGui_Input##type(label, &var)) \
    { \
        DS_CLAMP(var, <, rangelo) \
        DS_CLAMP(var, >, rangehi) \
        mcfg->Dirty = true; \
    }

typedef enum : int
{
    DispWinScaleMode_NoScale,
    DispWinScaleMode_Stretch,
    DispWinScaleMode_PreserveAspectRatio,
    DispWinScaleMode_Integer,

    DispWinScaleMode_MAX [[maybe_unused]],
} DispWinScaleMode;

typedef struct
{
    ImVec2 Sz;
    ImVec2 Pos;
    bool Bottom;
} Display;

typedef struct
{
    bool LockPos;
    bool NoDecor;
    DispWinScaleMode ScaleMode;
    bool Dirty;
    ImVec2 Sz;
    ImVec2 Pos;
    int NumDisplays;
    Display Display[GUI_MaxDisplaysPerWindow];
} DisplayWindow;

typedef struct
{
    u32 CurAddr9;
    char AddrText9[9];
    bool FollowA9;
    int A9BreakAddr;
    bool A9ExecBreak;
    bool A9DbgDisplay;

    u32 CurAddr7;
    char AddrText7[9];
    bool FollowA7;
    int A7BreakAddr;
    bool A7ExecBreak;
    bool A7DbgDisplay;
    bool Sched;
} DebugGui;

typedef struct
{
    SDL_Mutex* Mutex;
    GameCardCfg Cfg;
    char* SRAMPath;
    char* TxtPath;
    bool Show;
    char* EditFolder;
    bool InitSav;

    struct
    {
        char* FriendlyName;
        u32 Bitmap[32*32];
        u8 Size;
    } ROMDat;
    struct
    {
        u8 Size;
    } SRAMDat;
    struct
    {
        u32 ROMID;
        u32 SRAMID;
    } TxtDat;
    bool ROMDirty;
    bool SRAMDirty;
    bool TxtDirty;
    bool ROMDirtyB;
    bool SRAMDirtyB;
    bool TxtDirtyB;
    bool SavFromFile;
} NDSImportGui;

typedef struct
{
    SDL_Mutex* Mutex;
    char* NvramPath;
    bool Show;
    char* EditFolder;
    CoreCfg Cfg;
    bool InitNvram;

    struct
    {
        u8 NvramSize;
    } NvramDat;
    char TSCRange[4][4];
    int ConsoleType;
    bool NvramDirty;
    bool NTR7Dirty;
    bool NTR9Dirty;
    bool AGBDirty;
} SysImportGui;

typedef struct
{
    SDL_Mutex* Mutex;
    char* SRAMPath;
    bool Show;
    char* EditFolder;
    GamePakCfg Cfg;
    bool InitSav;

    bool ROMDirty;
    bool ROMDirtyB;
    bool SRAMDirty;
    bool SRAMDirtyB;
    bool SavFromFile;
} GBAImportGui;

typedef enum : u8
{
    LibType_Sys,
    LibType_NDS,
    LibType_GBA,

    LibType_MAX [[maybe_unused]],
} LibGui_Type;

constexpr char LibEmptyNames[LibType_MAX][8] =
{
[LibType_Sys] = "None",
[LibType_NDS] = "Ejected",
[LibType_GBA] = "Empty",
};

typedef struct
{
    int Num;
    const char* Paths[GUI_LibMax];
    const char* Names[GUI_LibMax];
} LibraryDat;

typedef struct
{
    SDL_Window* Win;
    SDL_Renderer* Ren;
    SDL_Texture* Top;
    SDL_Texture* Bot;
    DebugGui dbg;
    SysImportGui SysImportGui;
    NDSImportGui NDSImportGui;
    GBAImportGui GBAImportGui;
    u32 UserEventBase;
    bool CfgDisplay;
    bool DemoDisplay;
    bool ShowList;

    bool Paused;
    bool FrameLimit;

    LibraryDat Libs[LibType_MAX];
    SDL_Texture* NDSLibIcons[GUI_LibMax];
    char* LibrarySelName[3];
    bool NDSOpen;
} MainGUI;

typedef struct MainCfg MainCfg;
typedef struct MailBox MailBox;

MainGUI MainGUI_Init(MainCfg* mcfg);
void MainGUI_Loop(Console* sys, MailBox* mailbox, MainGUI* mgui, MainCfg* mcfg, const bool active);
void Mailbox_UpdateTouch(MailBox* mailbox, u16 x, u16 y, bool touched);

void DebugGUI_Loop(MailBox* mail, MainGUI* mgui, Console* sys);

void LibraryGui_InitList(MainGUI* mgui, MainCfg* mcfg);
void LibraryGui_Loop(MainGUI* mgui, MainCfg* mcfg, const bool active);
