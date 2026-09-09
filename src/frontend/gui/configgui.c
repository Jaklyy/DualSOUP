#include <string.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_mutex.h>

#include "imgui/dcimgui.h"

#include "core/utils.h"
#include "frontend/soupparser/soupparser.h"
#include "configgui.h"
#include "maingui.h"




typedef struct
{
    SDL_Mutex* Mutex;
    char** Str;
    bool* Dirty;
} CfgCallback;

// checkme: is it correct to always treat this as if its on a separate thread?
// can the mutex result in a deadlock?
void SDLCALL ConfigGUI_RecieveFileName(void* userdata, const char* const* filelist, [[maybe_unused]] int filter)
{
    CfgCallback* cb = userdata;
    if (!filelist)
    {
        printf("SDL File Dialog Errored out %s\n", SDL_GetError());
    }
    else if (!*filelist) return;
    else
    {
        SDL_LockMutex(cb->Mutex);
        free(*cb->Str);
        *cb->Str = strdup(*filelist);
        *cb->Dirty = true;
        SDL_UnlockMutex(cb->Mutex);
    }
    free(userdata); // clean up heap allocation
}

static int FilePathTextCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        char** str = data->UserData;
        free(*str);
        data->Buf = (*str = calloc(1, (data->BufSize < 1) ? 1 : data->BufSize));
    }
    return 0;
}

#define SelectFile(cfgstr, filtername, filterpattern, nfilters) \
    ImGui_Text(filtername); \
    if (ImGui_Button("Browse...##"filtername)) \
    { \
        static const SDL_DialogFileFilter filters = {filtername, filterpattern}; \
        CfgCallback* cbdat = malloc(sizeof(CfgCallback)); /* must be allocated on the heap */ \
        *cbdat = (CfgCallback){mcfg->Mutex, &mcfg->CoreCfg.cfgstr, &mcfg->Dirty}; \
        SDL_ShowOpenFileDialog(ConfigGUI_RecieveFileName, cbdat, mgui->Win, &filters, nfilters, NULL, false); \
    } \
    ImGui_SameLine(); \
    ImGui_InputTextEx("##"filtername, mcfg->CoreCfg.cfgstr, strlen(mcfg->CoreCfg.cfgstr)+1, ImGuiInputTextFlags_ElideLeft|ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &mcfg->CoreCfg.cfgstr); \
    if (ImGui_IsItemDeactivatedAfterEdit()) \
    { \
        mcfg->Dirty = true; \
    }

void ConfigGUI_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    if (!mgui->CfgDisplay) return;
    ImGui_SetNextWindowSize((ImVec2){200, 200}, ImGuiCond_FirstUseEver);

    if (ImGui_Begin("Config", &mgui->CfgDisplay, ImGuiWindowFlags_None))
    {
        if (ImGui_BeginTabBar("ConfigTabBar", ImGuiTabBarFlags_None))
        {
            if (ImGui_BeginTabItem("Main", NULL, ImGuiTabItemFlags_None))
            {
                SelectFile(NTR.Bios7, "NDS ARM7 Bios", "bin;rom", 1)
                SelectFile(NTR.Bios9, "NDS ARM9 Bios", "bin;rom", 1)
                SelectFile(NTR.NVRAM, "NDS Firmware", "bin;rom;mem", 1)

                ImGui_SeparatorText("TSC Range");
                ImGui_InputText("TSC Left", mgui->TSCRange[0], sizeof(mgui->TSCRange[0]), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
                if (ImGui_IsItemDeactivatedAfterEdit())
                {
                    // TODO: add dirty flag once system configs are worked out?
                    mcfg->CoreCfg.SysCfg.TSCL = strtoul(mgui->TSCRange[0], NULL, 16);
                }
                ImGui_InputText("TSC Right", mgui->TSCRange[1], sizeof(mgui->TSCRange[1]), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
                if (ImGui_IsItemDeactivatedAfterEdit())
                {
                    // TODO: add dirty flag once system configs are worked out?
                    mcfg->CoreCfg.SysCfg.TSCR = strtoul(mgui->TSCRange[1], NULL, 16);
                }
                ImGui_InputText("TSC Top", mgui->TSCRange[2], sizeof(mgui->TSCRange[2]), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
                if (ImGui_IsItemDeactivatedAfterEdit())
                {
                    // TODO: add dirty flag once system configs are worked out?
                    mcfg->CoreCfg.SysCfg.TSCT = strtoul(mgui->TSCRange[2], NULL, 16);
                }
                ImGui_InputText("TSC Bottom", mgui->TSCRange[3], sizeof(mgui->TSCRange[3]), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
                if (ImGui_IsItemDeactivatedAfterEdit())
                {
                    // TODO: add dirty flag once system configs are worked out?
                    mcfg->CoreCfg.SysCfg.TSCB = strtoul(mgui->TSCRange[3], NULL, 16);
                }
                ImGui_EndTabItem();
            }
            if (ImGui_BeginTabItem("Layout", NULL, ImGuiTabItemFlags_None))
            {
                if (ImGui_InputIntEx("Window", &mcfg->GuiCfg.NumDisplayWindows, 1, 1, ImGuiInputFlags_None))
                {
                    DS_CLAMP(mcfg->GuiCfg.NumDisplayWindows, <, GUI_MinDisplayWindows)
                    DS_CLAMP(mcfg->GuiCfg.NumDisplayWindows, >, GUI_MaxDisplayWindows)
                    mcfg->Dirty = true;
                }
                ImGui_EndTabItem();
            }
            ImGui_EndTabBar();
        }
    }
    ImGui_End();
}
