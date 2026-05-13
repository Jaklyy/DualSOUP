#include <string.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_mutex.h>

#include "../../../libs/imgui/dcimgui.h"

#include "../../core/utils.h"
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
    if (ImGui_ButtonEx("Browse...##"filtername, (ImVec2){100, 0})) \
    { \
        SDL_ShowOpenFileDialog(ConfigGUI_RecieveFileName, &(CfgCallback){corecfg->Mutex, &corecfg->cfgstr, &corecfg->Dirty}, mgui->Win, &(SDL_DialogFileFilter){filtername, filterpattern}, nfilters, NULL, false); \
    } \
    ImGui_SameLine(); \
    ImGui_InputTextEx("##"filtername, corecfg->cfgstr, strlen(corecfg->cfgstr)+1, ImGuiInputTextFlags_ElideLeft|ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &corecfg->cfgstr); \
    if (ImGui_IsItemDeactivatedAfterEdit()) \
    { \
        corecfg->Dirty = true; \
    }

void ConfigGUI_Loop(MainGUI* mgui, CoreCfg* corecfg)
{
    if (!mgui->CfgDisplay) return;
    ImGui_SetNextWindowSize((ImVec2){200, 200}, ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;

    if (ImGui_Begin("Config", &mgui->CfgDisplay, flags))
    {
        SelectFile(NTR.Bios7, "NDS ARM7 Bios", "bin;rom", 1)
        SelectFile(NTR.Bios9, "NDS ARM9 Bios", "bin;rom", 1)
        SelectFile(NTR.NVRAM, "NDS Firmware", "bin;rom;mem", 1)
    }
    ImGui_End();
}
