#include <stdlib.h>
#include <SDL3/SDL_filesystem.h>

#include "imgui/dcimgui.h"

#include "../main.h"
#include "maingui.h"
#include "../soupparser/soupparser.h"
#include "core/utils.h"


typedef struct LibGui_EnumPass
{
    const char** LibPaths;
    const char** LibNames;
    SDL_Texture** LibIcons;
    LibGui_Type Type;
    int* LibNum;
} LibGui_EnumPass;

SDL_EnumerationResult SDLCALL LibraryGui_EnumerateLoop(void* userdata, const char* dirname [[maybe_unused]], const char* fname)
{
    LibGui_EnumPass* pass = userdata;

    char folder[strlen(dirname)+strlen(fname)+sizeof("/")];
    strcpy(folder, dirname);
    strcat(folder, fname);
    strcat(folder, "/");
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(folder, &info) || (info.type != SDL_PATHTYPE_DIRECTORY))
        return SDL_ENUM_CONTINUE;

    printf("loading: %s\n", folder);

    char cfg[strlen(folder)+sizeof(DSCfgName)];
    strcpy(cfg, folder);
    strcat(cfg, DSCfgName);

    if (!SDL_GetPathInfo(cfg, &info) || (info.type != SDL_PATHTYPE_FILE))
        return SDL_ENUM_CONTINUE;

    switch(pass->Type)
    {
    case LibType_Sys:
    {
        GameCardCfg cfgtmp = {};
        Config_Load(cfg, &cfgtmp, GameCardCfgData, countof(GameCardCfgData), NULL, NULL);
        pass->LibPaths[*pass->LibNum] = strdup(folder);
        pass->LibNames[*pass->LibNum] = strdup(cfgtmp.FriendlyName);
        break;
    }
    case LibType_NDS:
    {
        CoreCfg cfgtmp = {};
        Config_Load(cfg, &cfgtmp, CoreCfgData, countof(CoreCfgData), NULL, NULL);
        pass->LibPaths[*pass->LibNum] = strdup(folder);
        pass->LibNames[*pass->LibNum] = strdup(cfgtmp.FriendlyName);
        break;
    }
    case LibType_GBA:
    {
        printf("GRAOOWWWWWWWWW\n");
        break;
    }
    }

    if (pass->LibIcons != nullptr)
    {
        char icon[strlen(folder)+sizeof(DSIconFile)];
        strcpy(icon, folder);
        strcat(icon, DSIconFile);
        size_t size;
        void* buf = SDL_LoadFile(icon, &size);
        if ((buf != NULL) && (size == (32*32*4)))
        {
            if (!SDL_UpdateTexture(pass->LibIcons[*pass->LibNum], NULL, buf, 4*32))
                printf("%s\n", SDL_GetError());
        }
        else
        {
            if (buf != NULL)  SDL_free(buf);
            u8 nobodysHome[32*32*8] = {};
            if (SDL_UpdateTexture(pass->LibIcons[*pass->LibNum], NULL, nobodysHome, 4*32))
                printf("%s\n", SDL_GetError());
        }
    }
    (*pass->LibNum)++;
    return (*pass->LibNum >= GUI_LibMax) ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
}

void LibraryGui_InitList(MainGUI* mgui)
{
    for (int i = 0; i < LibType_GBA/*LibType_MAX*/; i++)
    {
        const char* folder[] = {
            [LibType_Sys]=ConsoleFolder,
            [LibType_NDS]=NDSFolder,
            [LibType_GBA]=GBAFolder,
        };

        char librarypath[strlen(DualSOUPFolder)+strlen(folder[i])+1];
        strcpy(librarypath, DualSOUPFolder);
        strcat(librarypath, folder[i]);

        mgui->Libs[i].Num = 0;
        LibGui_EnumPass pass = {
            .LibPaths = mgui->Libs[i].Paths,
            .LibNames = mgui->Libs[i].Names,
            .LibIcons = ((i == LibType_NDS) ? mgui->NDSLibIcons : nullptr),
            .Type = i,
            .LibNum = &mgui->Libs[i].Num,
        };

        for (int i = 0; i < *pass.LibNum; i++)
        {
            free((void*)mgui->Libs[i].Paths);
            free((void*)mgui->Libs[i].Names);
        }

        SDL_EnumerateDirectory(librarypath, LibraryGui_EnumerateLoop, &pass);
    }
}

void LibraryRender(MainGUI* mgui, const LibGui_Type type)
{
    if (ImGui_BeginTable("Library", ((type == LibType_NDS) ? 2 : 1), ImGuiTableFlags_Hideable|ImGuiTableFlags_Sortable|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY))
    {
        if (type == LibType_NDS) ImGui_TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
        ImGui_TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
        ImGuiListClipper clip;
        ImGuiListClipper_Begin(&clip, mgui->Libs[type].Num, -1);
        while (ImGuiListClipper_Step(&clip))
        {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++)
            {
                ImGui_TableNextRow();
                if (i % 2) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBg), -1);
                else       ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBgAlt), -1);

                if (type == LibType_NDS)
                {
                    ImGui_TableNextColumn();
                    ImGui_Dummy((ImVec2){0.0, 0.0});
                    ImGui_Image((ImTextureRef){._TexID = (intptr_t)(mgui->NDSLibIcons[i])}, (ImVec2){32,32});
                }

                ImGui_TableNextColumn();
                ImGui_PushIDInt(i);
                if (ImGui_SelectableEx(mgui->Libs[type].Names[i], false, ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap, (ImVec2){0, 39}))
                {
                    mgui->Libs[type].Sel = i;
                }
                ImGui_PopID();
            }
        }
        ImGui_EndTable();
    }
}

void LibraryGui_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    if (ImGui_Button("Boot Console"))
    {
        {
            const char* path = mgui->Libs[LibType_NDS].Paths[mgui->Libs[LibType_NDS].Sel];
            char souppath[strlen(path)+sizeof(DSCfgName)];
            strcpy(souppath, path);
            strcat(souppath, DSCfgName);
            Config_Load(souppath, &mcfg->CoreCfg.NTR.GameCard, GameCardCfgData, countof(GameCardCfgData), nullptr, nullptr);
            mcfg->CoreCfg.NTR.GameCard.FolderPath = strdup(path);
        }
        {
            const char* path = mgui->Libs[LibType_Sys].Paths[mgui->Libs[LibType_Sys].Sel];
            char souppath[strlen(path)+sizeof(DSCfgName)];
            strcpy(souppath, path);
            strcat(souppath, DSCfgName);
            Config_Load(souppath, &mcfg->CoreCfg, CoreCfgData, countof(CoreCfgData), nullptr, nullptr);
            mcfg->CoreCfg.FolderPath = strdup(path);

            mcfg->CoreCfg.SysCfg = SysCfg_Defaults[mcfg->CoreCfg.Model];
        }
        SDL_Event evt = {.user = {.type = mgui->UserEventBase+UserEvent_BootRom}};
        if (!SDL_PushEvent(&evt)) printf("%s\n", SDL_GetError());
        mgui->ShowList = false;
    }
    ImGui_BeginTabBar("Libraries", 0);
    {
        if (ImGui_BeginTabItem("Console", NULL, 0))
        {
            if (ImGui_Button("Configure Consoles")) mgui->SysImportGui.Show = !mgui->SysImportGui.Show;

            LibraryRender(mgui, LibType_Sys);
            ImGui_EndTabItem();
        }

        if (ImGui_BeginTabItem("NDS Slot", NULL, 0))
        {
            if (ImGui_Button("Import NDS")) mgui->NDSImportGui.Show = !mgui->NDSImportGui.Show;

            LibraryRender(mgui, LibType_NDS);
            ImGui_EndTabItem();
        }

        ImGui_BeginDisabled(true);
        if (ImGui_BeginTabItem("GBA Slot", NULL, 0))
        {
            if (ImGui_Button("Import GBA")) mgui->GBAImportGui.Show = !mgui->GBAImportGui.Show;

            LibraryRender(mgui, LibType_NDS);
            ImGui_EndTabItem();
        }
        ImGui_EndDisabled();
        ImGui_EndTabBar();
    }
}
