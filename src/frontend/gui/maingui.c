#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "../../../libs/imgui/dcimgui.h"
#include "../../../libs/imgui/dcimgui_impl_sdl3.h"
#include "../../../libs/imgui/dcimgui_impl_sdlrenderer3.h"

#include "maingui.h"
#include "configgui.h"

#include "../../core/console.h"




MainGUI MainGUI_Init()
{
    MainGUI mgui = {};
    if (!SDL_CreateWindowAndRenderer("DualSOUP", 256*2, 192*2*2, /*SDL_WINDOW_RESIZABLE*/ /* TODO */ 0, &mgui.Win, &mgui.Ren))
    {
        printf("window/renderer init failure :(\n");
        exit(EXIT_FAILURE);
    }

    mgui.Blit = SDL_CreateTexture(mgui.Ren, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, 256, 192*2);

    SDL_SetTextureScaleMode(mgui.Blit,
#if SDL_VERSION_ATLEAST(3, 4, 0)
        SDL_SCALEMODE_PIXELART
#else
        SDL_SCALEMODE_NEAREST
#endif
        );

    ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable; 
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; 

    ImGui_StyleColorsDark(NULL);

    cImGui_ImplSDL3_InitForSDLRenderer(mgui.Win, mgui.Ren);
    cImGui_ImplSDLRenderer3_Init(mgui.Ren);

    return mgui;
}

bool MainGUI_Loop(struct Console* sys, MainGUI* mgui, CoreCfg* corecfg)
{
    cImGui_ImplSDLRenderer3_NewFrame();
    cImGui_ImplSDL3_NewFrame();
    ImGui_NewFrame();

    if (ImGui_BeginMainMenuBar())
    {
        if (ImGui_MenuItemBoolPtr("Config", NULL, &mgui->CfgDisplay, true)) {}
        if (ImGui_MenuItemBoolPtr("GUI Demo", NULL, &mgui->DemoDisplay, true)) {}
        ImGui_EndMainMenuBar();
    }

    ConfigGUI_Loop(mgui, corecfg);
    if (mgui->DemoDisplay) ImGui_ShowDemoWindow(&mgui->DemoDisplay);

    ImGui_Render();

    if (sys && !sys->Powman.PowerCR.SystemShutDown)
    {
        if (SDL_TryLockMutex(sys->FrameBufferMutex[mgui->Buffer]))
        {
            u8* buffer;
            int pitch; 
            SDL_LockTexture(mgui->Blit, NULL, (void**)&buffer, &pitch);
            for (int s = 0; s < 2; s++)
                for (int y = 0; y < 192; y++)
                    for (int x = 0; x < pitch/4; x++)
                        for (int b = 0; b < 4; b++)
                        {
                            if (b == 4) continue;
                            buffer[(s*192*pitch)+(y*pitch)+(x*pitch/256)+b] = (u8)((((float)((sys->Framebuffer[mgui->Buffer][s][y][x] >> (b*6)) & 0x3F) * 0xFF) / 0x3F));
                        }
            SDL_UnlockTexture(mgui->Blit);
            SDL_UnlockMutex(sys->FrameBufferMutex[mgui->Buffer]);
            mgui->Buffer = !mgui->Buffer;
            SDL_RenderTexture(mgui->Ren, mgui->Blit, NULL, NULL);
            cImGui_ImplSDLRenderer3_RenderDrawData(ImGui_GetDrawData(), mgui->Ren);
            SDL_RenderPresent(mgui->Ren);
            char str[256] = "";
            snprintf(str, 256, "DualSOUP - %f ms - %f ms", sys->FrameTime, sys->FrameTimeActual);
            SDL_SetWindowTitle(mgui->Win, str);
        }
        return true;
    }
    else
    {
        SDL_RenderClear(mgui->Ren);
        cImGui_ImplSDLRenderer3_RenderDrawData(ImGui_GetDrawData(), mgui->Ren);
        SDL_RenderPresent(mgui->Ren);
        SDL_SetWindowTitle(mgui->Win, "DualSOUP");
        return false;
    }
}


