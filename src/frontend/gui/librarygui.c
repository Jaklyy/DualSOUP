#include <SDL3/SDL_filesystem.h>
#include "imgui/dcimgui.h"

#include "../main.h"
#include "maingui.h"
#include "../soupparser/soupparser.h"
#include "core/utils.h"


typedef struct LibraryGui_EnumPass
{
    const char** LibraryFolders;
    SDL_Texture** LibraryIcons;
    int* LibraryNum;
} LibraryGui_EnumPass;

SDL_EnumerationResult SDLCALL LibraryGui_EnumerateLoop(void* userdata, const char* dirname [[maybe_unused]], const char* fname)
{
    LibraryGui_EnumPass* pass = userdata;

    pass->LibraryFolders[*pass->LibraryNum] = strdup(fname);
    char icon[strlen(dirname)+strlen(fname)+sizeof("/icon.bin")];
    strcpy(icon, dirname);
    strcat(icon, fname);
    strcat(icon, "/icon.bin");
    size_t size;
    printf("%s\n", icon);
    void* buf = SDL_LoadFile(icon, &size);
    if ((buf != NULL) && (size == (32*32*4)))
    {
        printf("yes???\n");
        if (!SDL_UpdateTexture(pass->LibraryIcons[*pass->LibraryNum], NULL, buf, 4*32))
            printf("%s\n", SDL_GetError());
    }
    else
    {
        if (buf != NULL)  SDL_free(buf);
        u8 nobodysHome[32*32*8] = {};
        if (SDL_UpdateTexture(pass->LibraryIcons[*pass->LibraryNum], NULL, nobodysHome, 4*32))
            printf("%s\n", SDL_GetError());
    }
    (*pass->LibraryNum)++;
    return (*pass->LibraryNum >= GUI_LibraryMax) ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
}

void LibraryGui_InitList(MainGUI* mgui)
{
    char* path = SDL_GetPrefPath("DualSOUP", "DualSOUP");
    constexpr char ndsfolder[] = "GameCard/";
    char* librarypath = malloc(strlen(path)+sizeof(ndsfolder));
    strcpy(librarypath, path);
    strcat(librarypath, ndsfolder);
    SDL_free(path);

    mgui->LibraryNum = 0;
    LibraryGui_EnumPass pass = {
        .LibraryFolders = mgui->LibraryFolders,
        .LibraryIcons = mgui->LibraryIcons,
        .LibraryNum = &mgui->LibraryNum,
    };

    for (int i = 0; i < *pass.LibraryNum; i++)
    {
        free((void*)mgui->LibraryFolders[i]);
    }

    SDL_EnumerateDirectory(librarypath, LibraryGui_EnumerateLoop, &pass);
    free(librarypath);
}

void LibraryGui_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    if (ImGui_Button("Import NDS")) mgui->NDSImportGui.Show = !mgui->NDSImportGui.Show ;
    ImGui_SameLine();
    if (ImGui_Button("Import GBA")) mgui->GBAImportGui.Show = !mgui->GBAImportGui.Show;

    if (ImGui_BeginTable("library", 2, ImGuiTableFlags_Hideable|ImGuiTableFlags_Sortable|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY))
    {
        ImGui_TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
        ImGui_TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
        ImGuiListClipper clip;
        ImGuiListClipper_Begin(&clip, mgui->LibraryNum, -1);
        while (ImGuiListClipper_Step(&clip))
        {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++)
            {
                ImGui_TableNextRow();
                if (i % 2) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBg), -1);
                else       ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBgAlt), -1);
                ImGui_TableNextColumn();
                ImGui_Image((ImTextureRef){._TexID = (intptr_t)(mgui->LibraryIcons[i])}, (ImVec2){32,32});
                ImGui_TableNextColumn();
                ImGui_PushIDInt(i);
                if (ImGui_SelectableEx(mgui->LibraryFolders[i], false, ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap, (ImVec2){0, 32}))
                {
                    char* path = SDL_GetPrefPath("DualSOUP", "DualSOUP");
                    constexpr char ndsfolder[] = "GameCard/";
                    constexpr char cfgsoup[] = "/cfg.soup";
                    char* gamepath = malloc(strlen(path)+sizeof(ndsfolder)+strlen(mgui->LibraryFolders[i])+sizeof(cfgsoup));
                    strcpy(gamepath, path);
                    strcat(gamepath, ndsfolder);
                    strcat(gamepath, mgui->LibraryFolders[i]);
                    strcat(gamepath, cfgsoup);
                    SDL_free(path);

                    Config_Load(gamepath, &mcfg->CoreCfg.SysCfg.GameCard, GameCardCfgData, countof(GameCardCfgData), nullptr, nullptr);

                    free(gamepath);

                    SDL_Event evt = {.user = {.type = mgui->UserEventBase+UserEvent_BootRom}};
                    if (!SDL_PushEvent(&evt)) printf("%s\n", SDL_GetError());
                    mgui->ShowList = false;
                }
                ImGui_PopID();
            }
        }
        ImGui_EndTable();
    }
}
