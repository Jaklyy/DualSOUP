#pragma once
#include <SDL3/SDL.h>

#include "../../../libs/imgui/dcimgui.h"

#include "../../core/console.h"
#include "../../core/utils.h"




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
    SDL_Window* Win;
    SDL_Renderer* Ren;
    SDL_Texture* Top;
    SDL_Texture* Bot;
    bool Buffer;
    bool CfgDisplay;
    bool DemoDisplay;

    char TSCRange[4][4];
} MainGUI;

typedef struct MainCfg MainCfg;
MainGUI MainGUI_Init(MainCfg* mcfg);
bool MainGUI_Loop(struct Console* sys, MainGUI* mgui, MainCfg* mcfg);
