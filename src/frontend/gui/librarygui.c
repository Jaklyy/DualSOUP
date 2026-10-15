#include <stdlib.h>
#include <SDL3/SDL_filesystem.h>

#include "imgui/dcimgui.h"

#include "../main.h"
#include "maingui.h"
#include "configgui.h"
#include "../soupparser/soupparser.h"
#include "core/utils.h"

constexpr u32 LibStrMax = 20;
char* LibrarySelName_Construct(const char* str)
{
    // parse utf-8 string
    size_t inlen = strlen(str);
    const char* pstr = str;
    for (u32 i = 0; i < LibStrMax; i++)
    {
        const char* prev = pstr;
        u32 codepoint = SDL_StepUTF8(&pstr, &inlen);

        // sanitize characters
        switch(codepoint)
        {
        case u8'\n': // new line; terminate string
        {
            pstr = prev;
            goto done;
        }
        default: break;
        }
    }
    done:
    // allocate out string
    size_t outlen = pstr-str;
    char* outstr = malloc(outlen+1);
    memcpy(outstr, str, outlen);
    outstr[outlen] = u8'\0';
    return outstr;
}

typedef struct LibGui_EnumPass
{
    const char** LibPaths;
    const char** LibNames;
    SDL_Texture** LibIcons;
    const char* Sel;
    char** NameOut;
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

    if (strcmp(pass->LibPaths[*pass->LibNum], pass->Sel) == 0)
    {
        free((void*)*pass->NameOut);
        *pass->NameOut = LibrarySelName_Construct(pass->LibNames[*pass->LibNum]);
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

void LibraryGui_InitList(MainGUI* mgui, MainCfg* mcfg)
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
            .Sel = mcfg->GuiCfg.LibrarySel[i],
            .NameOut = &mgui->LibrarySelName[i],
            .Type = i,
            .LibNum = &mgui->Libs[i].Num,
        };

        for (int i = 0; i < *pass.LibNum; i++)
        {
            free((void*)mgui->Libs[i].Paths);
            free((void*)mgui->Libs[i].Names);
        }

        SDL_EnumerateDirectory(librarypath, LibraryGui_EnumerateLoop, &pass);
        if (mgui->LibrarySelName[i] == NULL)
        {
            free(mcfg->GuiCfg.LibrarySel[i]);
            mcfg->GuiCfg.LibrarySel[i] = calloc(1, 1);
            mgui->LibrarySelName[i] = strdup(LibEmptyNames[i]);
        }
    }
}

void LibraryRender(MainGUI* mgui, MainCfg* mcfg, const LibGui_Type type)
{
    if (ImGui_BeginTable("Library", (3 + (type == LibType_NDS)), ImGuiTableFlags_Hideable|ImGuiTableFlags_Sortable|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY))
    {
        if (type == LibType_NDS) ImGui_TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
        ImGui_TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui_TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed);
        ImGui_TableSetupColumn("Open", ImGuiTableColumnFlags_WidthFixed);
        ImGuiListClipper clip;
        ImGuiListClipper_Begin(&clip, mgui->Libs[type].Num, -1);
        while (ImGuiListClipper_Step(&clip))
        {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++)
            {
                ImGui_PushIDInt(i);

                int cur = i;

                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                if (i % 2) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBg), -1);
                else       ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBgAlt), -1);

                if (type == LibType_NDS)
                {
                    ImGui_Dummy((ImVec2){0.0, 0.0});
                    ImGui_Image((ImTextureRef){._TexID = (intptr_t)(mgui->NDSLibIcons[cur])}, (ImVec2){32,32});
                    ImGui_TableNextColumn();
                }

                if (ImGui_SelectableEx(mgui->Libs[type].Names[cur], strcmp(mcfg->GuiCfg.LibrarySel[type], mgui->Libs[type].Paths[cur]) == 0, ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap, (ImVec2){0, 39}))
                {
                    free(mcfg->GuiCfg.LibrarySel[type]);
                    mcfg->GuiCfg.LibrarySel[type] = strdup(mgui->Libs[type].Paths[cur]);
                    free(mgui->LibrarySelName[type]);
                    mgui->LibrarySelName[type] = LibrarySelName_Construct(mgui->Libs[type].Names[cur]);
                    mcfg->Dirty = true;
                }

                ImGui_TableNextColumn();
                if (ImGui_Button("Edit"))
                {
                    switch(type)
                    {
                    case LibType_Sys: SysImportGui_Toggle(mgui, mgui->Libs[type].Paths[cur]); break;
                    case LibType_NDS: NDSImportGui_Toggle(mgui, mgui->Libs[type].Paths[cur]); break;
                    case LibType_GBA: CrashSpectacularly("GBAAAAAAA\n");
                    }
                }
                ImGui_TableNextColumn();
                if (ImGui_Button("Open Folder"))
                {
                    char path[sizeof("file://")+strlen(mgui->Libs[type].Paths[cur])];
                    strcpy(path, "file://");
                    strcat(path, mgui->Libs[type].Paths[cur]);
                    if (!SDL_OpenURL(path)) printf("Couldn't open folder: %s", SDL_GetError());
                }

                ImGui_PopID();
            }
        }
        ImGui_EndTable();
    }
}

void LibraryGui_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    ImGui_BeginDisabled(mcfg->GuiCfg.LibrarySel[LibType_Sys][0] == '\0');
    if (ImGui_Button("Boot Console"))
    {
        if (mcfg->GuiCfg.LibrarySel[LibType_NDS][0] != '\0')
        {
            const char* path = mcfg->GuiCfg.LibrarySel[LibType_NDS];
            char souppath[strlen(path)+sizeof(DSCfgName)];
            strcpy(souppath, path);
            strcat(souppath, DSCfgName);
            Config_Load(souppath, &mcfg->CoreCfg.NTR.GameCard, GameCardCfgData, countof(GameCardCfgData), nullptr, nullptr);
            mcfg->CoreCfg.NTR.GameCard.FolderPath = strdup(path);
        }
        else mcfg->CoreCfg.NTR.GameCard.ROMBusType = GameCard_ROMBus_Ejected;

        {
            const char* path = mcfg->GuiCfg.LibrarySel[LibType_Sys];
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
    ImGui_EndDisabled();


#define AllocStupidString(title, type) \
    size_t len = strlen(mgui->LibrarySelName[type]); \
    char str[sizeof(#title": ###"#title)+len]; \
    char* neostr = str; \
    memcpy(neostr, #title": ", sizeof(#title": ")-1); \
    neostr += sizeof(#title": ")-1; \
    memcpy(neostr, mgui->LibrarySelName[type], len); \
    neostr += len; \
    memcpy(neostr, "###"#title, sizeof("###"#title));

    ImGui_BeginTabBar("Libraries", 0);
    {
        {
            AllocStupidString(Console, LibType_Sys)
            if (ImGui_BeginTabItem(str, NULL, 0))
            {
                if (ImGui_Button("Import Console")) SysImportGui_Toggle(mgui, nullptr);

                LibraryRender(mgui, mcfg, LibType_Sys);
                ImGui_EndTabItem();
            }
        }

        {
            ImGui_BeginDisabled(mcfg->GuiCfg.LibrarySel[LibType_Sys][0] == '\0');
            AllocStupidString(NDS Slot, LibType_NDS)
            if (ImGui_BeginTabItem(str, NULL, (mgui->NDSOpen ? ImGuiTabItemFlags_SetSelected : 0)))
            {
                if (ImGui_Button("Import NDS")) NDSImportGui_Toggle(mgui, nullptr);
                ImGui_SameLine();
                if (ImGui_Button("Eject Card"))
                {
                    free(mcfg->GuiCfg.LibrarySel[LibType_NDS]);
                    mcfg->GuiCfg.LibrarySel[LibType_NDS] = calloc(1, 1);
                    free(mgui->LibrarySelName[LibType_NDS]);
                    mgui->LibrarySelName[LibType_NDS] = strdup(LibEmptyNames[LibType_NDS]);
                    mcfg->Dirty = true;
                }

                LibraryRender(mgui, mcfg, LibType_NDS);
                ImGui_EndTabItem();
            }
            ImGui_EndDisabled();
            mgui->NDSOpen = false;
        }

        ImGui_BeginDisabled(true);
        {
            #if 0
            size_t len = strlen(mgui->LibrarySelName[LibType_GBA]);
            char str[sizeof("GBA Slot: ###GBA Slot")+len];
            char* neostr = str;
            memcpy(neostr, "GBA Slot: ", sizeof("GBA Slot: ")-1);
            neostr += sizeof("GBA Slot: ");
            memcpy(neostr, mgui->LibrarySelName[LibType_GBA], len);
            neostr += len+1;
            memcpy(neostr, "###GBA Slot", sizeof("###GBA Slot"));

            if (ImGui_BeginTabItem(str, NULL, 0))
            {
                if (ImGui_Button("Import GBA")) mgui->GBAImportGui.Show = !mgui->GBAImportGui.Show;

                LibraryRender(mgui, mcfg, LibType_NDS);
                ImGui_EndTabItem();
            }
            #endif
        }
        ImGui_EndDisabled();
        ImGui_EndTabBar();
    }
}
