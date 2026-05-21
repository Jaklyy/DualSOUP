#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "../../../libs/imgui/dcimgui.h"
#include "../../../libs/imgui/dcimgui_impl_sdl3.h"
#include "../../../libs/imgui/dcimgui_impl_sdlrenderer3.h"

#include "maingui.h"
#include "configgui.h"
#include "../soupparser/soupparser.h"

#include "../../core/console.h"




MainGUI MainGUI_Init()
{
    MainGUI mgui = {};
    if (!SDL_CreateWindowAndRenderer("DualSOUP", 256*2, 192*2*2, SDL_WINDOW_RESIZABLE, &mgui.Win, &mgui.Ren))
    {
        printf("window/renderer init failure :(\n");
        exit(EXIT_FAILURE);
    }

    mgui.Top = SDL_CreateTexture(mgui.Ren, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
    mgui.Bot = SDL_CreateTexture(mgui.Ren, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);

#if SDL_VERSION_ATLEAST(3, 4, 0)
    #define FB_MODE SDL_SCALEMODE_PIXELART
#else
    #define FB_MODE SDL_SCALEMODE_NEAREST
#endif

    SDL_SetTextureScaleMode(mgui.Top, FB_MODE);
    SDL_SetTextureScaleMode(mgui.Bot, FB_MODE);
#undef FB_MODE

    ImGui_CreateContext(NULL);

    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable; 
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; 

    ImGui_StyleColorsDark(NULL);

    cImGui_ImplSDL3_InitForSDLRenderer(mgui.Win, mgui.Ren);
    cImGui_ImplSDLRenderer3_Init(mgui.Ren);

    return mgui;
}

bool MainGUI_Loop(struct Console* sys, MainGUI* mgui, MainCfg* mcfg)
{
    GuiCfg* gcfg = &mcfg->GuiCfg;
    SysCfg* scfg = &mcfg->CoreCfg.SysCfg;
    cImGui_ImplSDLRenderer3_NewFrame();
    cImGui_ImplSDL3_NewFrame();
    ImGui_NewFrame();

    ImGui_DockSpaceOverViewport();

    if (ImGui_BeginMainMenuBar())
    {
        if (ImGui_MenuItemBoolPtr("Config", NULL, &mgui->CfgDisplay, true)) {}
        if (ImGui_MenuItemBoolPtr("GUI Demo", NULL, &mgui->DemoDisplay, true)) {}
        ImGui_EndMainMenuBar();
    }

    ConfigGUI_Loop(mgui, mcfg);
    if (mgui->DemoDisplay) ImGui_ShowDemoWindow(&mgui->DemoDisplay);

    bool active = (sys && !sys->Powman.PowerCR.SystemShutDown);
    if (active)
    {
        if (SDL_TryLockMutex(sys->FrameBufferMutex[mgui->Buffer]))
        {
            u8* buffer;
            int pitch; 
            for (int s = 0; s < 2; s++)
            {
                SDL_LockTexture(((s == 0) ? mgui->Top : mgui->Bot), NULL, (void**)&buffer, &pitch);
                for (int y = 0; y < 192; y++)
                    for (int x = 0; x < pitch/4; x++)
                        for (int b = 0; b < 4; b++)
                        {
                            if (b == 4) continue;
                            buffer[(y*pitch)+(x*pitch/256)+b] = (u8)((((float)((sys->Framebuffer[mgui->Buffer][s][y][x] >> (b*6)) & 0x3F) * 0xFF) / 0x3F));
                        }
                SDL_UnlockTexture(((s == 0) ? mgui->Top : mgui->Bot));
            }
            sys->TSC.State.Touched = false;
            SDL_UnlockMutex(sys->FrameBufferMutex[mgui->Buffer]);
            mgui->Buffer = !mgui->Buffer;
        }
    }
    else
    {
        // TODO: this only really needs to be done once
        u8* buffer;
        int pitch;
        for (int s = 0; s < 2; s++)
        {
            SDL_LockTexture(((s == 0) ? mgui->Top : mgui->Bot), NULL, (void**)&buffer, &pitch);
                for (int y = 0; y < 192; y++)
                    for (int x = 0; x < pitch/4; x++)
                        for (int b = 0; b < 4; b++)
                        {
                            if (b == 4) continue;
                            buffer[(y*pitch)+(x*pitch/256)+b] = 0;
                        }
            SDL_UnlockTexture(((s == 0) ? mgui->Top : mgui->Bot));
        }
    }

    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){0,0});
    ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui_PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui_PushStyleVar(ImGuiStyleVar_ImageRounding, 0);
    ImGui_PushStyleVar(ImGuiStyleVar_ImageBorderSize, 0);

    char label[] = "Display Window##0";
    for (int i = 0; i < gcfg->NumDisplayWindows; i++)
    {
        DisplayWindow* dispwin = &gcfg->DisplayWindow[i];
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoFocusOnAppearing;
        if (dispwin->LockPos) flags |= ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize;
        if (dispwin->NoDecor) flags |= ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoCollapse;

        static_assert((('0'+(GUI_MaxDisplayWindows)) <= '9') || (('0'+(GUI_MaxDisplayWindows)) >= '0'), "This code needs to be updated to work with > 10 windows\n");
        label[sizeof(label)/sizeof(label[0])-2] += 1;

        if (dispwin->Dirty)
        {
            ImGui_SetNextWindowSize(dispwin->Sz, ImGuiCond_Always);
            ImGui_SetNextWindowPos(dispwin->Pos, ImGuiCond_Always);
            dispwin->Dirty = false;
        }
        else
        {
            ImGui_SetNextWindowSize((ImVec2){256,192}, ImGuiCond_FirstUseEver);
        }

        if (ImGui_Begin(label, NULL, flags))
        {
            ImVec2 sz = ImGui_GetContentRegionAvail();
            dispwin->Sz = ImGui_GetWindowSize();
            dispwin->Sz = ImGui_GetWindowPos();

            ImVec2 maxsz = {0.0,0.0};
            // TODO: dont calc this every time?
            for (int j = 0; j < dispwin->NumDisplays; j++)
            {
                Display* disp = &dispwin->Display[j];
                float xmax = disp->Pos.x + disp->Sz.x;
                DS_CLAMP(maxsz.x, <, xmax)
                float ymax = disp->Pos.y + disp->Sz.y;
                DS_CLAMP(maxsz.y, <, ymax)
            }

            ImVec2 scale;
            if (maxsz.x == 0.0 || maxsz.y == 0.0)
            {
                // prevent div by 0
                scale = (ImVec2){1, 1};
            }
            else
            {
                switch(dispwin->ScaleMode)
                {
                case DispWinScaleMode_NoScale:
                    scale = (ImVec2){1, 1};
                    break;
                case DispWinScaleMode_Stretch:
                    scale = (ImVec2){sz.x / maxsz.x, sz.y / maxsz.y};
                    break;
                case DispWinScaleMode_PreserveAspectRatio:
                    scale = (ImVec2){sz.x / maxsz.x, sz.y / maxsz.y};
                    DS_CLAMP(scale.x, >, scale.y)
                    DS_CLAMP(scale.y, >, scale.x)
                    break;
                case DispWinScaleMode_Integer:
                    scale = (ImVec2){(s64)(sz.x / maxsz.x), (s64)(sz.y / maxsz.y)};
                    DS_CLAMP(scale.x, >, scale.y)
                    DS_CLAMP(scale.y, >, scale.x)
                    // min 1x scale
                    DS_CLAMP(scale.x, <, 1)
                    DS_CLAMP(scale.y, <, 1)
                    break;
                }
            }

            // right click menu for configuration
            if (ImGui_BeginPopupContextWindow())
            {
                ImGui_SeparatorText("Display Window Settings");
                ImGui_Checkbox("Lock Window", &dispwin->LockPos);
                ImGui_Checkbox("Display Only", &dispwin->NoDecor);

                GUI_INPUTCLAMPED(Float, "X Pos", dispwin->Pos.x, DisplayWindowPosX)
                GUI_INPUTCLAMPED(Float, "Y Pos", dispwin->Pos.y, DisplayWindowPosY)
                GUI_INPUTCLAMPED(Float, "Width", dispwin->Sz.x, DisplayWindowWidth)
                GUI_INPUTCLAMPED(Float, "Height", dispwin->Sz.x, DisplayWindowHeight)

                ImGui_SeparatorText("Per Window Display Settings");

                GUI_INPUTCLAMPED(Int, "Num Displays", dispwin->NumDisplays, DisplaysPerWindow)
                const char* inputs[] = {"No Scaling", "Stetch", "Maintain Aspect Ratio", "Integer Scale"};
                if (ImGui_ComboChar("Scaling Mode", &dispwin->ScaleMode, inputs, sizeof(inputs)/sizeof(inputs[0])))
                {
                    mcfg->Dirty = true;
                }

                char displabel[] = "Display 0 Settings";

                for (int j = 0; j < dispwin->NumDisplays; j++)
                {
                    ImGui_PushIDInt(j);

                    static_assert((('0'+(GUI_MaxDisplaysPerWindow)) <= '9') || (('0'+(GUI_MaxDisplaysPerWindow)) >= '0'), "This code needs to be updated to work with > 10 displays\n");
                    displabel[(sizeof("Display 0") / sizeof(displabel[0])) - 2] += 1;
                    ImGui_SeparatorText(displabel);
                    Display* disp = &dispwin->Display[j];

                    GUI_INPUTCLAMPED(Float, "X Pos", disp->Pos.x, DisplayPosX)
                    GUI_INPUTCLAMPED(Float, "Y Pos", disp->Pos.y, DisplayPosY)
                    GUI_INPUTCLAMPED(Float, "Width", disp->Sz.x, DisplayWidth)
                    GUI_INPUTCLAMPED(Float, "Height", disp->Sz.y, DisplayHeight)

                    ImGui_PopID();
                }

                ImGui_EndPopup();
            }

            ImVec2 basepos = ImGui_GetCursorStartPos();
            for (int j = 0; j < dispwin->NumDisplays; j++)
            {
                Display* disp = &dispwin->Display[j];

                // scale position and size
                ImGui_SetCursorPos((ImVec2){basepos.x + (disp->Pos.x * scale.x), basepos.y + (disp->Pos.y * scale.y)});
                ImVec2 dispsz = {disp->Sz.x * scale.x, disp->Sz.y * scale.y};

                // calculate tsc touch coords if needed
                if (active && disp->Bottom && ImGui_IsMouseDown(ImGuiMouseButton_Left) && ImGui_IsItemHovered(ImGuiHoveredFlags_None))
                {
                    ImVec2 curpos = ImGui_GetCursorScreenPos();
                    ImVec2 moupos = ImGui_GetMousePos();
                    if (dispsz.x == 0.0) sys->TSC.State.X = 0; // dont div by 0 pls
                    else sys->TSC.State.X = ((moupos.x-curpos.x) * (scfg->TSCR - scfg->TSCL) / dispsz.x) + scfg->TSCL;
                    if (dispsz.y == 0.0) sys->TSC.State.Y = 0; // dont div by 0 pls
                    else sys->TSC.State.Y = ((moupos.y-curpos.y) * (scfg->TSCB - scfg->TSCT) / dispsz.y) + scfg->TSCT;
                    sys->TSC.State.Touched = true;
                }

                ImGui_Image((ImTextureRef){._TexID = (intptr_t)(disp->Bottom ? mgui->Bot : mgui->Top)}, dispsz);
            }
        }
        ImGui_End();
    }

    ImGui_PopStyleVarEx(5);

    ImGui_Render();
    SDL_RenderClear(mgui->Ren);
    cImGui_ImplSDLRenderer3_RenderDrawData(ImGui_GetDrawData(), mgui->Ren);
    SDL_RenderPresent(mgui->Ren);

    if (active)
    {
        char str[256] = "";
        snprintf(str, 256, "DualSOUP - %f ms - %f ms", sys->FrameTime, sys->FrameTimeActual);
        SDL_SetWindowTitle(mgui->Win, str);
    }
    else SDL_SetWindowTitle(mgui->Win, "DualSOUP");

    return active;
}
