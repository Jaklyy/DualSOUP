#pragma once
#include <SDL3/SDL.h>
#include "../../core/console.h"
#include "../../core/utils.h"



typedef struct
{
    SDL_Window* Win;
    SDL_Renderer* Ren;
    SDL_Texture* Blit;
    bool Buffer;
    bool CfgDisplay;
    bool DemoDisplay;
} MainGUI;

MainGUI MainGUI_Init();
bool MainGUI_Loop(struct Console* sys, MainGUI* mgui, CoreCfg* corecfg);
