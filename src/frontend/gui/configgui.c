#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_stdinc.h>

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

#define SelectFile(cfgstr, filtername, filterpattern, nfilters, mutex, dirty) \
    ImGui_Text(filtername); \
    if (ImGui_Button("Browse...##"filtername)) \
    { \
        static const SDL_DialogFileFilter filters = {filtername, filterpattern}; \
        CfgCallback* cbdat = malloc(sizeof(CfgCallback)); /* must be allocated on the heap */ \
        *cbdat = (CfgCallback){mutex, &cfgstr, &dirty}; \
        SDL_ShowOpenFileDialog(ConfigGUI_RecieveFileName, cbdat, mgui->Win, &filters, nfilters, NULL, false); \
    } \
    ImGui_SameLine(); \
    ImGui_PushItemFlag(ImGuiItemFlags_LiveEditOnInput, false); \
    ImGui_InputTextEx("##"filtername, cfgstr, strlen(cfgstr)+1, ImGuiInputTextFlags_ElideLeft|ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &cfgstr); \
    ImGui_PopItemFlag(); \
    if (ImGui_IsItemDeactivatedAfterEdit()) \
    { \
        dirty = true; \
    }

void ConfigGUI_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    if (!mgui->CfgDisplay) return;
    ImGui_SetNextWindowSize((ImVec2){408, 372}, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Config", &mgui->CfgDisplay, ImGuiWindowFlags_None))
    {
        ImGui_End();
        return;
    }

    if (ImGui_BeginTabBar("ConfigTabBar", ImGuiTabBarFlags_None))
    {
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
        if (ImGui_BeginTabItem("Logging", NULL, ImGuiTabItemFlags_None))
        {
            if (ImGui_Button("Disable All\n")) 
            {
                mcfg->LoggingMask = (LogMask = 0);
                mcfg->Dirty = true;
            }
            ImGui_SameLine();
            if (ImGui_Button("Enable All\n")) 
            {
                mcfg->LoggingMask = (LogMask = (LOG_MAX-1));
                mcfg->Dirty = true;
            }

            for (u32 i = 0; i < stdc_trailing_zeros((u64)LOG_MAX); i++)
            {
                ImGui_PushIDInt(i);
                bool pass = LogMask & (1 << i);
                if (ImGui_Checkbox(LoggingLevelsNames[i], &pass))
                {
                    mcfg->LoggingMask = (LogMask ^= (1<<i));
                    mcfg->Dirty = true;
                }
                ImGui_PopID();
            }
            ImGui_EndTabItem();
        }
        if (ImGui_TabItemButton("Open DualSOUP Folder", 0))
        {
            char path[sizeof("file://")+strlen(DualSOUPFolder)];
            strcpy(path, "file://");
            strcat(path, DualSOUPFolder);
            if (!SDL_OpenURL(path)) printf("Couldn't open folder: %s", SDL_GetError());
        }
        ImGui_EndTabBar();
    }
    ImGui_End();
}

void ImportGui_ParseNDSROM(NDSImportGui* igui)
{
    if (!igui->ROMDirty) return;
    igui->ROMDirty = false;

    SDL_LockMutex(igui->Mutex);

    igui->ROMDat.Size = 0;
    memset(igui->ROMDat.Bitmap, 0, sizeof(igui->ROMDat.Bitmap));

    SDL_IOStream* rom;
    if ((rom = SDL_IOFromFile(igui->Cfg.ROMPath, "rb")) != NULL)
    {
        s64 sz;
        if ((sz = SDL_GetIOSize(rom)) <= 0) goto kill;

        igui->ROMDat.Size = ceil(log2(sz));

        if (SDL_SeekIO(rom, 0x68, SDL_IO_SEEK_SET) != 0x68) goto kill;
        u32 iconoffs = 0;
        if ((SDL_ReadIO(rom, &iconoffs, 4) != 4) || (iconoffs == 0)) goto kill;
        if (SDL_SeekIO(rom, iconoffs+0x20, SDL_IO_SEEK_SET) != (iconoffs+0x20)) goto kill;

        {
            struct
            {
                u8 bitmap[0x200];
                u16 pal[16];
            } icontmp;

            if (SDL_ReadIO(rom, &icontmp, 0x220) != 0x220) goto kill;
            u32 pal[16];
            pal[0] = 0;
            for (u8 i = 1; i < 16; i++)
            {
                pal[i]  = (((icontmp.pal[i] << 1) & 0x3C) * 0xFF / 0x3F) << 0;
                pal[i] |= ((((icontmp.pal[i] >> 4) & 0x3C) | ((icontmp.pal[i] >> 15) & 1)) * 0xFF / 0x3F) << 8;
                pal[i] |= (((icontmp.pal[i] >> 9) & 0x3C) * 0xFF / 0x3F) << 16;
                pal[i] |= 0xFF000000;
            }
            u8 xm = 0;
            u8 ym = 0;
            u8 x = 0;
            u8 y = 0;
            u32 i = 0;
            while (true)
            {
                igui->ROMDat.Bitmap[(xm|x) + ((ym|y)*32)] = pal[((icontmp.bitmap[i/2] >> ((i%2)*4)) & 0xF)];
                x++;
                i++;
                if (x == 8)
                {
                    y++;
                    x = 0;
                }
                if (y == 8)
                {
                    y = 0;
                    xm += 8;
                }
                if (xm == 32)
                {
                    xm = 0;
                    ym += 8;
                }
                if (ym == 32) break;
            }
        }

        if (SDL_SeekIO(rom, iconoffs+0x340, SDL_IO_SEEK_SET) != (iconoffs+0x340)) goto kill;

        {
            char str[(128)*2] = {0};
            if (SDL_ReadIO(rom, str, (sizeof(str)-sizeof(str[0]))) != (sizeof(str)-sizeof(str[0]))) goto kill;

            if (igui->ROMDat.FriendlyName != NULL)
                SDL_free(igui->ROMDat.FriendlyName);

            igui->ROMDat.FriendlyName = SDL_iconv_string("UTF-8", "UTF-16", str, sizeof(str));
        }
        kill:
        SDL_CloseIO(rom);
    }
    SDL_UnlockMutex(igui->Mutex);
}

void ImportGui_ParseNDSSRAM(NDSImportGui* igui)
{
    if (!igui->SRAMDirty) return;
    igui->SRAMDirty = false;

    SDL_LockMutex(igui->Mutex);
    igui->SRAMDat.Size = 0;

    SDL_IOStream* sram;
    if ((sram = SDL_IOFromFile(igui->SRAMPath, "rb")) != NULL)
    {

        s64 sz;
        if ((sz = SDL_GetIOSize(sram)) <= 0) goto kill;

        igui->SRAMDat.Size = ceil(log2(sz));
        igui->SavFromFile = true;

        kill:
        SDL_CloseIO(sram);
    }
    SDL_UnlockMutex(igui->Mutex);
}

// note: this is probably not a standard meant to be stable, but w/e.
// we can work on a proper standard later.
void ImportGui_ParseNDSTxt(NDSImportGui* igui)
{
    if (!igui->TxtDirty) return;
    igui->TxtDirty = false;

    SDL_LockMutex(igui->Mutex);

    igui->TxtDat.ROMID = 0;
    igui->TxtDat.SRAMID = 0xFFFFFF;

    SDL_IOStream* txt;
    if ((txt = SDL_IOFromFile(igui->TxtPath, "rt")) != NULL)
    {
        s64 sz;
        if ((sz = SDL_GetIOSize(txt)) <= 0) goto kill2;
        char* buf;
        if ((buf = calloc(sz+1, 1)) == NULL) goto kill2;

        if (SDL_ReadIO(txt, buf, sz) != (size_t)sz) goto kill1; // idk

        char* find;
        if ((find = strstr(buf, "Cart ID      : ")) != NULL)
        {
            find += (sizeof("Cart ID      : ")-1);
            igui->TxtDat.ROMID = (u32)strtoull(find, NULL, 16);
            printf("%08X\n",igui->TxtDat.ROMID);
        }
        if ((find = strstr(buf, "Save chip ID : ")) != NULL)
        {
            find += (sizeof("Save chip ID : ")-1);
            igui->TxtDat.SRAMID = (u32)strtoull(find, NULL, 16) & 0xFFFFFF;
            printf("%06X\n",igui->TxtDat.SRAMID);
        }

        kill1:
        free(buf);
        kill2:
        SDL_CloseIO(txt);
    }
    SDL_UnlockMutex(igui->Mutex);
}

char* FindFileWithDifferentExtension(char* path, char* ext)
{
    char* end = strrchr(path, '.');
    if (end == NULL) return NULL; // no extension, idc

    char* out = calloc((end-path)+1+strlen(ext)+1, 1);
    strncpy(out, path, (end-path)+1);
    strcpy(out+(end-path)+1, ext);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(out, &info)) return NULL;
    if (info.type != SDL_PATHTYPE_FILE) return NULL;
    return out;
}

bool GetLibraryString(char* path)
{
    size_t base = strlen(path);
    int i = 0;
    for (; i <= GUI_LibMax; i++)
    {
        snprintf(&path[base], sizeof(LibraryFolderPH), LibraryFolderFmt, i);
        SDL_ClearError();
        if (!SDL_GetPathInfo(path, NULL)) break; // fuck if i know how to use this properly
    }

    if (i == GUI_LibMax)
    {
        printf("arbitrary folder limit reached: kill the developer\n");
        return false;
    }
    return true;
}

void SysImportGui_Toggle(MainGUI* mgui, const char* editfolder)
{
    SysImportGui* igui = &mgui->SysImportGui;
    CoreCfg* cfg = &igui->Cfg;
    bool prevedit = (igui->EditFolder != nullptr);
    bool newedit = (editfolder != nullptr);
    if (igui->Show && (prevedit == newedit) && (!prevedit || !newedit || !strcmp(igui->EditFolder, editfolder)))
    {
        igui->Show = false;
        return;
    }
    igui->Show = true;
    free(igui->EditFolder);
    igui->EditFolder = ((editfolder == nullptr) ? nullptr : strdup(editfolder));
    igui->NvramPath[0] = '\0';
    igui->InitNvram = (igui->EditFolder == nullptr);

    memset(&igui->Cfg, 0, (sizeof(SysImportGui) - offsetof(SysImportGui, NvramDat)));

    char* souppath = nullptr;
    if (editfolder != nullptr)
    {
        if ((souppath = SDL_stack_alloc(char, strlen(editfolder)+sizeof(DSCfgName))) == NULL)
            CrashSpectacularly("stack alloc failure...\n");
        strcpy(souppath, editfolder);
        strcat(souppath, DSCfgName);
    }

    Config_Load(souppath, cfg, CoreCfgData, countof(CoreCfgData), NULL, NULL);
    if (souppath != nullptr) SDL_stack_free(souppath);
    igui->ConsoleType = cfg->Model-ConsoleModel_NTR001; // TEMP
    for (int i = 0; i < 4; i++) snprintf(igui->TSCRange[i], 4, "%03X", cfg->TSC.Arr[i]);
}

void SysImportGui_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    SysImportGui* igui = &mgui->SysImportGui;
    CoreCfg* cfg = &igui->Cfg;
    if (!igui->Show) return;

    ImGui_SetNextWindowSize((ImVec2){340.0, (igui->EditFolder != nullptr) ? 444.0 : 422.0}, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin(((igui->EditFolder != nullptr) ? "Edit DS System" : "Import DS System"), &igui->Show, 0))
        return ImGui_End();


    if (ImGui_ComboChar("Console Type", &igui->ConsoleType, &ConsoleModelsStr[1], 2))
    {
        cfg->Model = igui->ConsoleType + ConsoleModel_NTR001; // TEMP
    }

    ImGui_InputTextEx("Name", cfg->FriendlyName, strlen(cfg->FriendlyName)+1, ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &cfg->FriendlyName);

    ImGui_SeparatorText("Files");

    SelectFile(cfg->NTR.Bios7, "NDS ARM7 Bios", "bin;rom", 1, igui->Mutex, igui->NTR7Dirty)
    SelectFile(cfg->NTR.Bios9, "NDS ARM9 Bios", "bin;rom", 1, igui->Mutex, igui->NTR9Dirty)
    SelectFile(cfg->AGB.Bios7, "AGB Bios", "bin;rom", 1, igui->Mutex, igui->AGBDirty)
    SelectFile(igui->NvramPath, "NDS Firmware", "bin;rom;mem", 1, igui->Mutex, igui->NvramDirty)

    if (igui->NvramDirty)
    {
        igui->NvramDirty = false;
        igui->InitNvram = (igui->EditFolder == nullptr) || (igui->NvramPath[0] != '\0');
    }

    if (igui->EditFolder != nullptr)
    {
        ImGui_BeginDisabled(igui->NvramPath[0] == '\0');
        ImGui_Checkbox("Reimport Firmware", &igui->InitNvram);
        ImGui_EndDisabled();
    }

    ImGui_SeparatorText("TSC Range");
    for (int i = 0; i < 4; i++)
    {
        const char* strs[4] = {"TSC Left", "TSC Right", "TSC Top", "TSC Bottom"};
        ImGui_InputText(strs[i], igui->TSCRange[i], sizeof(igui->TSCRange[i]), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
        if (ImGui_IsItemDeactivatedAfterEdit()) // TODO: add dirty flag once system configs are worked out?
            cfg->TSC.Arr[i] = strtoul(igui->TSCRange[i], NULL, 16);
    }

    ImGui_SeparatorText("");

    bool hasName = (cfg->FriendlyName[0] != '\0');
    bool hasFirmware = (!igui->InitNvram || (igui->NvramPath[0] != '\0'));
    bool hasBios7 = (cfg->NTR.Bios7[0] != '\0');
    bool hasBios9 = (cfg->NTR.Bios9[0] != '\0');
    ImGui_BeginDisabled(!hasName || !hasFirmware || !hasBios7 || !hasBios9);
    if (ImGui_Button((igui->EditFolder != nullptr) ? "Update" : "Import"))
    {
        // todo: sanitize filepath
        char* basepath;
        if (igui->EditFolder != nullptr)
        {
            basepath = igui->EditFolder;
        }
        else
        {
            basepath = SDL_stack_alloc(char, strlen(DualSOUPFolder)+sizeof(ConsoleFolder)+sizeof(LibraryFolderPH)-1);
            if (basepath == NULL)
            {
                printf("alloca failure :C\n");
                return;
            }
            strcpy(basepath, DualSOUPFolder);
            strcat(basepath, ConsoleFolder);

            if (!GetLibraryString(basepath))
                return SDL_stack_free(basepath);

            SDL_CreateDirectory(basepath);
        }

        char cfgfile[strlen(basepath)+sizeof(DSCfgName)];
        strcpy(cfgfile, basepath);
        strcat(cfgfile, DSCfgName);

        Config_Write(cfgfile, cfg, CoreCfgData, countof(CoreCfgData), nullptr, nullptr);

        if (igui->InitNvram)
        {
            char nvramfile[strlen(basepath)+sizeof(ConsoleWiFiNvram)];
            strcpy(nvramfile, basepath);
            strcat(nvramfile, ConsoleWiFiNvram);

            SDL_CopyFile(igui->NvramPath, nvramfile);
        }

        if (basepath != igui->EditFolder) SDL_stack_free(basepath);
        igui->Show = false;
        // refresh library list
        LibraryGui_InitList(mgui, mcfg);
    }
    ImGui_EndDisabled();
    if (!hasName || !hasFirmware || !hasBios7 || !hasBios9)
    {
        if (ImGui_BeginItemTooltip())
        {
            if (!hasName)     ImGui_Text("You must provide a name!");
            if (!hasFirmware) ImGui_Text("You must provide a firmware image!");
            if (!hasBios7)    ImGui_Text("You must provide an NDS ARM7 Bios!");
            if (!hasBios9)    ImGui_Text("You must provide an NDS ARM9 Bios!");
            ImGui_EndTooltip();
        }
    }
    ImGui_End();
}

void NDSImportGui_Infer(NDSImportGui* igui, GameCardCfg* cfg)
{
    ImportGui_ParseNDSROM(igui);
    ImportGui_ParseNDSSRAM(igui);
    ImportGui_ParseNDSTxt(igui);

    if (igui->ROMDat.FriendlyName != NULL)
    {
        free(cfg->FriendlyName);
        cfg->FriendlyName = strdup(igui->ROMDat.FriendlyName);
    }

    if (igui->TxtDat.ROMID != 0)
    {
        cfg->ROMChipID = igui->TxtDat.ROMID;
        u8 szbyte = (igui->TxtDat.ROMID >> 8) & 0xFF;
        if (szbyte < 0x80)
        {
            szbyte+=1;
            if (stdc_count_ones(szbyte) != 1)
            {
                printf("Unknown ROM ChipID size field: %08"PRIX8"\n", igui->TxtDat.ROMID);
                goto romSizeError;
            }
            cfg->ROMChipSize = stdc_trailing_zeros(szbyte) + 20;
        }
        else if (szbyte > 0xF0)
        {
            u32 sz = (0x100-szbyte) * MiB(256);
            if (stdc_count_ones(sz) != 1)
            {
                printf("Unknown ROM ChipID size field: %08"PRIX8"\n", igui->TxtDat.ROMID);
                goto romSizeError;
            }
            cfg->ROMChipSize = stdc_trailing_zeros(sz);
        }
        else
        {
            printf("Unknown ROM ChipID size field: %08"PRIX8"\n", igui->TxtDat.ROMID);
            goto romSizeError;
        }
    }
    else
    {
        romSizeError:
        if (igui->ROMDat.Size != 0)
        {
            cfg->ROMChipSize = igui->ROMDat.Size;
            DS_CLAMP(cfg->ROMChipSize, <, CTZ_CONSTEXPR(MiB(1)));
        }
    }

    printf("%u\n", cfg->ROMChipSize);

    cfg->SPIBusType = GameCard_SPIBus_DirectSRAM;

    if (igui->SRAMDat.Size != 0)
    {
        cfg->SRAMChipSize = igui->SRAMDat.Size;
        DS_CLAMP(cfg->SRAMChipSize, <, CTZ_CONSTEXPR((int)512));
    }

    if (igui->TxtDat.SRAMID == 0xFFFFFF)
    {
        if (cfg->SRAMChipSize == 9)
        {
            cfg->SRAMChipType = GameCard_SRAMChip_EEPROM9BitAddr;
        }
        else if (cfg->SRAMChipSize <= 16)
        {
            cfg->SRAMChipType = GameCard_SRAMChip_EEPROM16BitAddr;
        }
        else
        {
            cfg->SRAMChipType = GameCard_SRAMChip_EEPROM24BitAddr;
        }
        cfg->FlashChipID = 0xFFFFFF;
    }
    else
    {
        cfg->SRAMChipType = GameCard_SRAMChip_Flash24BitAddr;
        cfg->FlashChipID = igui->TxtDat.SRAMID;
    }
}


void NDSImportGui_Toggle(MainGUI* mgui, const char* editfolder)
{
    NDSImportGui* igui = &mgui->NDSImportGui;
    GameCardCfg* cfg = &igui->Cfg;
    bool prevedit = (igui->EditFolder != nullptr);
    bool newedit = (editfolder != nullptr);
    if (igui->Show && (prevedit == newedit) && (!prevedit || !newedit || !strcmp(igui->EditFolder, editfolder)))
    {
        igui->Show = false;
        return;
    }
    igui->Show = true;
    free(igui->EditFolder);
    igui->EditFolder = ((editfolder == nullptr) ? nullptr : strdup(editfolder));
    igui->InitSav = (igui->EditFolder == nullptr);

    char* souppath = nullptr;
    if (editfolder != nullptr)
    {
        if ((souppath = SDL_stack_alloc(char, strlen(editfolder)+sizeof(DSCfgName))) == NULL)
            CrashSpectacularly("stack alloc failure...\n");
        strcpy(souppath, editfolder);
        strcat(souppath, DSCfgName);
    }
    Config_Load(souppath, cfg, GameCardCfgData, countof(GameCardCfgData), NULL, NULL);
    igui->SRAMPath[0] = '\0';
    igui->TxtPath[0] = '\0';
    SDL_free(igui->ROMDat.FriendlyName);
    memset(&igui->ROMDat, 0, sizeof(NDSImportGui) - offsetof(NDSImportGui, ROMDat));
    if (souppath != nullptr) SDL_stack_free(souppath);
    igui->TxtDat.SRAMID = 0xFFFFFF;
}

void NDSImportGui_Update(MainGUI* mgui, MainCfg* mcfg)
{
    NDSImportGui* igui = &mgui->NDSImportGui;
    GameCardCfg* cfg = &igui->Cfg;

    size_t sramsize = ((size_t)1)<<cfg->SRAMChipSize;
    u8 srambuffer[sramsize];

    size_t basefolderlen = strlen(DualSOUPFolder)+(sizeof(NDSFolder))+(sizeof(LibraryFolderPH)-1);
    // base folder name
    char* basepath;
    if (igui->EditFolder != nullptr)
    {
        basepath = igui->EditFolder;
    }
    else
    {
        basepath = SDL_stack_alloc(char, strlen(DualSOUPFolder)+sizeof(NDSFolder)+sizeof(LibraryFolderPH)-1);
        if (basepath == NULL) return (void)printf("alloca failure :C\n");
        strcpy(basepath, DualSOUPFolder);
        strcat(basepath, NDSFolder);

        if (!GetLibraryString(basepath))
            return SDL_stack_free(basepath);
    }

    char cfgpath[basefolderlen+sizeof(DSCfgName)];
    char savfolderpath[basefolderlen+sizeof(DSSavFolder)];
    char savpath[basefolderlen+sizeof(DSSavFolder)+sizeof(DSSavFile)];
    char iconpath[basefolderlen+sizeof(DSIconFile)];

    // copy to other paths
    strcpy(savfolderpath, basepath);
    strcpy(iconpath, basepath);
    strcpy(cfgpath, basepath);

    // finish set up
    strcat(cfgpath, DSCfgName);
    strcat(iconpath, DSIconFile);
    strcat(savfolderpath, DSSavFolder);
    // make folder
    if (!SDL_CreateDirectory(savfolderpath))
    {
        printf("Directory creation failure: %s\n", SDL_GetError());
        goto exit;
    }
    // copy one more time
    strcpy(savpath, savfolderpath);
    strcat(savpath, DSSavFile);
    printf("%s\n", basepath);

    Config_Write(cfgpath, cfg, GameCardCfgData, countof(GameCardCfgData), nullptr, igui->Mutex);

    if (!igui->InitSav || (cfg->SPIBusType == GameCard_SPIBus_None) || (cfg->SRAMChipType == GameCard_SRAMChip_None)) // exit if no sram
        goto skipsav;
    memset(srambuffer, 0xFF, sramsize);

    // load provided save
    if (igui->SavFromFile && (igui->SRAMPath[0] != '\0'))
    {
        SDL_IOStream* file;
        if ((file = SDL_IOFromFile(igui->SRAMPath, "rb")) != NULL)
        {
            s64 sz;
            if ((sz = SDL_GetIOSize(file)) <= 0)
            {
                if (sz == 0) printf("SRAM file is empty.\n");
                else         printf("SRAM filesize get error. sz: %"PRIi64" %s\n", sz, SDL_GetError());
            }
            else if ((SDL_ReadIO(file, srambuffer, sz) != (u64)sz) && (SDL_GetIOStatus(file) != SDL_IO_STATUS_EOF))
                printf("SRAM File read error: %s\n", SDL_GetError());

            if (!SDL_CloseIO(file))
                printf("SRAM File close error: %s\n", SDL_GetError());
        }
        else printf("Could not open provided SRAM file %s\n", SDL_GetError());
    }

    // write save to destination
    {
        SDL_IOStream* file;
        if ((file = SDL_IOFromFile(savpath, "wb")) != NULL)
        {
            if (SDL_WriteIO(file, srambuffer, sramsize) != sramsize)
                printf("SRAM File write error: %s\n", SDL_GetError());

            if (!SDL_CloseIO(file))
                printf("SRAM File close error: %s\n", SDL_GetError());
        }
        else printf("Could not create file %s to write SRAM to %s\n", savpath, SDL_GetError());
    }
    skipsav:

    ImportGui_ParseNDSROM(igui);
    SDL_SaveFile(iconpath, igui->ROMDat.Bitmap, sizeof(igui->ROMDat.Bitmap));

    igui->Show = false;
    // refresh library list
    LibraryGui_InitList(mgui, mcfg);
    exit:
    if (basepath != igui->EditFolder) SDL_stack_free(basepath);
}

void NDSImportGui_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    NDSImportGui* igui = &mgui->NDSImportGui;
    GameCardCfg* cfg = &igui->Cfg;
    if (!igui->Show) return;

    ImGui_SetNextWindowSize((ImVec2){340.0, (igui->EditFolder != nullptr) ? 553.0 : 530.0}, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin(((igui->EditFolder != nullptr) ? "Edit NDS Game Card" : "Import NDS Game Card"), &igui->Show, 0))
        return ImGui_End();

    SelectFile(cfg->ROMPath, "NDS ROM", "nds", 1, igui->Mutex, igui->ROMDirtyB);
    igui->ROMDirty |= igui->ROMDirtyB;
    SelectFile(igui->SRAMPath, "NDS SRAM", "sav", 1, igui->Mutex, igui->SRAMDirtyB);
    igui->SRAMDirty |= igui->SRAMDirtyB;
    SelectFile(igui->TxtPath, "GM9 Dump Info", "txt", 1, igui->Mutex, igui->TxtDirtyB);
    igui->TxtDirty |= igui->TxtDirtyB;
    if (igui->ROMDirtyB || igui->SRAMDirtyB || igui->TxtDirtyB)
    {
        char* str = NULL;
        if (igui->ROMDirtyB) str = cfg->ROMPath;
        if (igui->SRAMDirtyB) str = igui->SRAMPath;
        if (igui->TxtDirtyB) str = igui->TxtPath;
        if (cfg->ROMPath[0] == '\0')
        {
            char* new;
            if (((new = FindFileWithDifferentExtension(str, "nds")) != NULL))
            {
                free(cfg->ROMPath);
                cfg->ROMPath = new;
                igui->ROMDirty = true;
            }
        }
        if (igui->SRAMPath[0] == '\0')
        {
            char* new;
            if (((new = FindFileWithDifferentExtension(str, "sav")) != NULL))
            {
                free(igui->SRAMPath);
                igui->SRAMPath = new;
                igui->SRAMDirty = true;
            }
        }
        if (igui->TxtPath[0] == '\0')
        {
            char* new;
            if (((new = FindFileWithDifferentExtension(str, "txt")) != NULL))
            {
                free(igui->TxtPath);
                igui->TxtPath = new;
                igui->TxtDirty = true;
            }
        }
    }

    // TODO: infer from game code?
    ImGui_Text("Note: Infer doesn't auto-select\nnon-standard card hw (ie. IR)");
    if (ImGui_Button("Infer from provided files\n"))
        NDSImportGui_Infer(igui, cfg);

    if (igui->EditFolder != nullptr) ImGui_Checkbox("Overwrite save\n", &igui->InitSav);
    ImGui_BeginDisabled(igui->SRAMPath[0] == '\0');
    ImGui_Checkbox("Use save from file\n", &igui->SavFromFile);
    ImGui_EndDisabled();

    ImGui_SeparatorText("");

    ImGui_InputTextMultilineEx("Game Title", cfg->FriendlyName, strlen(cfg->FriendlyName)+1, (ImVec2){0, 45}, ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &cfg->FriendlyName);


    ImGui_SeparatorText("ROM Chip");

    {
        const char* const romsizes[] = {"1 MiB", "2 MiB", "4 MiB", "8 MiB", "16 MiB", "32 MiB", "64 MiB", "128 MiB", "256 MiB", "512 MiB"};
        constexpr u64 romsizebase = CTZ_CONSTEXPR(MiB(1));
        u8 cursel = cfg->ROMChipSize-romsizebase;
        if (ImGui_BeginCombo("ROM Chip Size", romsizes[cursel], 0))
        {
            for (u64 i = 0; i < countof(romsizes); i++)
            {
                if (ImGui_SelectableEx(romsizes[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->ROMChipSize = i+romsizebase;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_InputIntEx("ROM Chip ID", (s32*)&cfg->ROMChipID, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);


    ImGui_SeparatorText("SPI/SRAM Chip");

    {
        const char* const spitype[] = {"None", "Direct SRAM Chip", "Infrared (HLE)"};
        u8 cursel = cfg->SPIBusType;
        if (ImGui_BeginCombo("SPI Bus Type", spitype[cursel], 0))
        {
            for (u64 i = 0; i < countof(spitype); i++)
            {
                if (ImGui_SelectableEx(spitype[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->SPIBusType = i;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_BeginDisabled(cfg->SPIBusType == GameCard_SPIBus_None);
    {
        const char* const sramchiptype[] = {"None", "Flash (Max: 16 MiB)", "EEPROM w/ 9 Bit Addr (Max: 512 Bytes)", "EEPROM w/ 16 Bit Addr (Max: 64 KiB)", "EEPROM w/ 24 Bit Addr (Max: 16 MiB)"};
        u8 cursel = cfg->SRAMChipType;
        if (ImGui_BeginCombo("SRAM Chip Type", sramchiptype[cursel], 0))
        {
            for (u64 i = 0; i < countof(sramchiptype); i++)
            {
                if (ImGui_SelectableEx(sramchiptype[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->SRAMChipType = i;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_BeginDisabled(cfg->SRAMChipType == GameCard_SRAMChip_None);
    {
        const char* const sramsizes[] = {"512 B", "1 KiB", "2 KiB", "4 KiB", "8 KiB", "16 KiB", "32 KiB", "64 KiB", "128 KiB", "256 KiB", "512 KiB", "1 MiB", "2 MiB", "4 MiB", "8 MiB", "16 MiB"};
        constexpr u64 sramsizebase = CTZ_CONSTEXPR((int)512);
        u8 cursel = cfg->SRAMChipSize - sramsizebase;
        if (ImGui_BeginCombo("SRAM Chip Size", sramsizes[cursel], 0))
        {
            for (u64 i = 0; i < countof(sramsizes); i++)
            {
                if (ImGui_SelectableEx(sramsizes[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->SRAMChipSize = i+sramsizebase;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }


    ImGui_BeginDisabled(cfg->SRAMChipType != GameCard_SRAMChip_Flash24BitAddr);
    ImGui_InputIntEx("Flash Chip ID", (s32*)&cfg->FlashChipID, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
    cfg->FlashChipID &= 0xFFFFFF;

    ImGui_EndDisabled();
    ImGui_EndDisabled();
    ImGui_EndDisabled();

    ImGui_SeparatorText("");

    bool hasROM = (cfg->ROMPath[0] != '\0');
    bool hasName = (cfg->FriendlyName[0] != '\0');
    ImGui_BeginDisabled(!hasROM || !hasName);
    if (ImGui_Button((igui->EditFolder != nullptr) ? "Update" : "Import") && (cfg->ROMPath[0] != '\0'))
        NDSImportGui_Update(mgui, mcfg);
    ImGui_EndDisabled();
    if (!hasROM || !hasName)
    {
        if (ImGui_BeginItemTooltip())
        {
            if (!hasName) ImGui_Text("You must provide a name!");
            if (!hasROM)  ImGui_Text("You must provide a ROM file!");
            ImGui_EndTooltip();
        }
    }

    ImGui_End();
}


void GBAImportGui_Toggle(MainGUI* mgui, const char* editfolder)
{
    GBAImportGui* igui = &mgui->GBAImportGui;
    GamePakCfg* cfg = &igui->Cfg;
    bool prevedit = (igui->EditFolder != nullptr);
    bool newedit = (editfolder != nullptr);
    if (igui->Show && (prevedit == newedit) && (!prevedit || !newedit || !strcmp(igui->EditFolder, editfolder)))
    {
        igui->Show = false;
        return;
    }
    igui->Show = true;
    free(igui->EditFolder);
    igui->EditFolder = ((editfolder == nullptr) ? nullptr : strdup(editfolder));
    igui->InitSav = (igui->EditFolder == nullptr);

    char* souppath = nullptr;
    if (editfolder != nullptr)
    {
        if ((souppath = SDL_stack_alloc(char, strlen(editfolder)+sizeof(DSCfgName))) == NULL)
            CrashSpectacularly("stack alloc failure...\n");
        strcpy(souppath, editfolder);
        strcat(souppath, DSCfgName);
    }
    Config_Load(souppath, cfg, GamePakCfgData, countof(GamePakCfgData), NULL, NULL);
    igui->SRAMPath[0] = '\0';
    memset(&igui->ROMDirty, 0, sizeof(GBAImportGui) - offsetof(GBAImportGui, ROMDirty));
    if (souppath != nullptr) SDL_stack_free(souppath);
}

void GBAImportGui_Update(MainGUI* mgui, MainCfg* mcfg)
{
    GBAImportGui* igui = &mgui->GBAImportGui;
    GamePakCfg* cfg = &igui->Cfg;

    size_t sramsize = ((size_t)1)<<cfg->SRAMChipSize;
    u8 srambuffer[sramsize];

    size_t basefolderlen = strlen(DualSOUPFolder)+(sizeof(GBAFolder))+(sizeof(LibraryFolderPH)-1);
    // base folder name
    char* basepath;
    if (igui->EditFolder != nullptr)
    {
        basepath = igui->EditFolder;
    }
    else
    {
        basepath = SDL_stack_alloc(char, strlen(DualSOUPFolder)+sizeof(GBAFolder)+sizeof(LibraryFolderPH)-1);
        if (basepath == NULL) return (void)printf("alloca failure :C\n");
        strcpy(basepath, DualSOUPFolder);
        strcat(basepath, GBAFolder);

        if (!GetLibraryString(basepath))
            return SDL_stack_free(basepath);
    }

    char cfgpath[basefolderlen+sizeof(DSCfgName)];
    char savfolderpath[basefolderlen+sizeof(DSSavFolder)];
    char savpath[basefolderlen+sizeof(DSSavFolder)+sizeof(DSSavFile)];

    // copy to other paths
    strcpy(savfolderpath, basepath);
    strcpy(cfgpath, basepath);

    // finish set up
    strcat(cfgpath, DSCfgName);
    strcat(savfolderpath, DSSavFolder);
    // make folder
    if (!SDL_CreateDirectory(savfolderpath))
    {
        printf("Directory creation failure: %s\n", SDL_GetError());
        goto exit;
    }
    // copy one more time
    strcpy(savpath, savfolderpath);
    strcat(savpath, DSSavFile);
    printf("%s\n", basepath);

    Config_Write(cfgpath, cfg, GamePakCfgData, countof(GamePakCfgData), nullptr, igui->Mutex);

    if (!igui->InitSav || (cfg->SRAMType == GamePakSRAM_None)) // exit if no sram
        goto skipsav;
    memset(srambuffer, 0xFF, sramsize);

    // load provided save
    if (igui->SavFromFile && (igui->SRAMPath[0] != '\0'))
    {
        SDL_IOStream* file;
        if ((file = SDL_IOFromFile(igui->SRAMPath, "rb")) != NULL)
        {
            s64 sz;
            if ((sz = SDL_GetIOSize(file)) <= 0)
            {
                if (sz == 0) printf("SRAM file is empty.\n");
                else         printf("SRAM filesize get error. sz: %"PRIi64" %s\n", sz, SDL_GetError());
            }
            else if ((SDL_ReadIO(file, srambuffer, sz) != (u64)sz) && (SDL_GetIOStatus(file) != SDL_IO_STATUS_EOF))
                printf("SRAM File read error: %s\n", SDL_GetError());

            if (!SDL_CloseIO(file))
                printf("SRAM File close error: %s\n", SDL_GetError());
        }
        else printf("Could not open provided SRAM file %s\n", SDL_GetError());
    }

    // write save to destination
    {
        SDL_IOStream* file;
        if ((file = SDL_IOFromFile(savpath, "wb")) != NULL)
        {
            if (SDL_WriteIO(file, srambuffer, sramsize) != sramsize)
                printf("SRAM File write error: %s\n", SDL_GetError());

            if (!SDL_CloseIO(file))
                printf("SRAM File close error: %s\n", SDL_GetError());
        }
        else printf("Could not create file %s to write SRAM to %s\n", savpath, SDL_GetError());
    }
    skipsav:

    igui->Show = false;
    // refresh library list
    LibraryGui_InitList(mgui, mcfg);
    exit:
    if (basepath != igui->EditFolder) SDL_stack_free(basepath);
}

void GBAImportGui_Loop(MainGUI* mgui, MainCfg* mcfg)
{
    GBAImportGui* igui = &mgui->GBAImportGui;
    GamePakCfg* cfg = &igui->Cfg;
    if (!igui->Show) return;

    ImGui_SetNextWindowSize((ImVec2){340.0, (igui->EditFolder != nullptr) ? 436.0 : 414.0}, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin(((igui->EditFolder != nullptr) ? "Edit GBA Game Pak" : "Import GBA Game Pak"), &igui->Show, 0))
        return ImGui_End();

    SelectFile(cfg->ROMPath, "GBA ROM", "gba", 1, igui->Mutex, igui->ROMDirtyB);
    igui->ROMDirty |= igui->ROMDirtyB;
    SelectFile(igui->SRAMPath, "GBA SRAM", "sav", 1, igui->Mutex, igui->SRAMDirtyB);
    igui->SRAMDirty |= igui->SRAMDirtyB;

    if (igui->ROMDirtyB || igui->SRAMDirtyB)
    {
        char* str = NULL;
        if (igui->ROMDirtyB) str = cfg->ROMPath;
        if (igui->SRAMDirtyB) str = igui->SRAMPath;
        if (cfg->ROMPath[0] == '\0')
        {
            char* new;
            if (((new = FindFileWithDifferentExtension(str, "gba")) != NULL))
            {
                free(cfg->ROMPath);
                cfg->ROMPath = new;
                igui->ROMDirty = true;
            }
        }
        if (igui->SRAMPath[0] == '\0')
        {
            char* new;
            if (((new = FindFileWithDifferentExtension(str, "sav")) != NULL))
            {
                free(igui->SRAMPath);
                igui->SRAMPath = new;
                igui->SRAMDirty = true;
            }
        }
    }

    //if (ImGui_Button("Infer from provided files\n"))
    //    GBAImportGui_Infer(igui, cfg);

    if (igui->EditFolder != nullptr) ImGui_Checkbox("Overwrite save\n", &igui->InitSav);
    ImGui_BeginDisabled(igui->SRAMPath[0] == '\0');
    ImGui_Checkbox("Use save from file\n", &igui->SavFromFile);
    ImGui_EndDisabled();

    ImGui_SeparatorText("");

    ImGui_InputTextMultilineEx("Game Title", cfg->FriendlyName, strlen(cfg->FriendlyName)+1, (ImVec2){0, 45}, ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &cfg->FriendlyName);

    ImGui_SeparatorText("ROM Chip");

    {
        const char* const romsizes[] = {"1 MiB", "2 MiB", "4 MiB", "8 MiB", "16 MiB", "32 MiB"};
        constexpr u64 romsizebase = CTZ_CONSTEXPR(MiB(1));
        u8 cursel = cfg->ROMChipSize-romsizebase;
        if (ImGui_BeginCombo("ROM Chip Size", romsizes[cursel], 0))
        {
            for (u64 i = 0; i < countof(romsizes); i++)
            {
                if (ImGui_SelectableEx(romsizes[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->ROMChipSize = i+romsizebase;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_SeparatorText("SPI/SRAM Chip");

    {
        const char* const sramtype[] = {"None", "SRAM", "Flash"};
        u8 cursel = cfg->SRAMType;
        if (ImGui_BeginCombo("SRAM Chip Type", sramtype[cursel], 0))
        {
            for (u64 i = 0; i < countof(sramtype); i++)
            {
                if (ImGui_SelectableEx(sramtype[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->SRAMType = i;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_BeginDisabled(cfg->SRAMType == GamePakSRAM_None);
    {
        const char* const sramsizes[] = {"512 B", "1 KiB", "2 KiB", "4 KiB", "8 KiB", "16 KiB", "32 KiB", "64 KiB", "128 KiB"};
        constexpr u64 sramsizebase = CTZ_CONSTEXPR((int)512);
        u8 cursel = cfg->SRAMChipSize - sramsizebase;
        if (ImGui_BeginCombo("SRAM Chip Size", sramsizes[cursel], 0))
        {
            for (u64 i = 0; i < countof(sramsizes); i++)
            {
                if (ImGui_SelectableEx(sramsizes[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->SRAMChipSize = i+sramsizebase;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_BeginDisabled(cfg->SRAMType != GamePakSRAM_Flash);
    {
        const char* const flashtypes[] = {
            [GamePakFlash_Atmel]="Atmel",
            [GamePakFlash_SST]="SST",
            [GamePakFlash_Panasonic]="Panasonic",
            [GamePakFlash_Macronix]="Macronix",
            [GamePakFlash_Sanyo]="Sanyo"};
        u8 cursel = cfg->GamePakFlashType;
        if (ImGui_BeginCombo("Flash Chip Variant", flashtypes[cursel], 0))
        {
            for (u64 i = 0; i < countof(flashtypes); i++)
            {
                if (ImGui_SelectableEx(flashtypes[i], cursel==i, 0, (ImVec2){0,0}))
                    cfg->GamePakFlashType = i;

                if (cursel==i) ImGui_SetItemDefaultFocus();
            }
            ImGui_EndCombo();
        }
    }

    ImGui_InputIntEx("Flash Chip ID", &cfg->FlashChipID_Int, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
    cfg->FlashChipID &= 0xFFFF;

    ImGui_EndDisabled();
    ImGui_EndDisabled();

    ImGui_SeparatorText("");

    bool hasROM = (cfg->ROMPath[0] != '\0');
    bool hasName = (cfg->FriendlyName[0] != '\0');
    ImGui_BeginDisabled(!hasROM || !hasName);
    if (ImGui_Button((igui->EditFolder != nullptr) ? "Update" : "Import") && (cfg->ROMPath[0] != '\0'))
       GBAImportGui_Update(mgui, mcfg);

    ImGui_EndDisabled();
    if (!hasROM || !hasName)
    {
        if (ImGui_BeginItemTooltip())
        {
            if (!hasName) ImGui_Text("You must provide a name!");
            if (!hasROM)  ImGui_Text("You must provide a ROM file!");
            ImGui_EndTooltip();
        }
    }

    ImGui_End();

}
