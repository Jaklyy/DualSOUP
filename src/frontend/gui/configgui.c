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
    ImGui_SetNextWindowSize((ImVec2){284, 292}, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Config", &mgui->CfgDisplay, ImGuiWindowFlags_None))
    {
        ImGui_End();
        return;
    }

    if (ImGui_BeginTabBar("ConfigTabBar", ImGuiTabBarFlags_None))
    {
        if (ImGui_BeginTabItem("Main", NULL, ImGuiTabItemFlags_None))
        {
            SelectFile(mcfg->CoreCfg.NTR.Bios7, "NDS ARM7 Bios", "bin;rom", 1, mcfg->Mutex, mcfg->Dirty)
            SelectFile(mcfg->CoreCfg.NTR.Bios9, "NDS ARM9 Bios", "bin;rom", 1, mcfg->Mutex, mcfg->Dirty)
            SelectFile(mcfg->CoreCfg.NTR.NVRAM, "NDS Firmware", "bin;rom;mem", 1, mcfg->Mutex, mcfg->Dirty)

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
    ImGui_End();
}

void ImportGui_ParseNDSROM(ImportGui* igui)
{
    if (!igui->ROMDirty) return;
    igui->ROMDirty = false;

    SDL_LockMutex(igui->Mutex);

    igui->ROMDat.Size = 0;
    memset(igui->ROMDat.Bitmap, 0, sizeof(igui->ROMDat.Bitmap));

    SDL_IOStream* rom;
    if ((rom = SDL_IOFromFile(igui->ROMPath, "rb")) != NULL)
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

void ImportGui_ParseNDSSRAM(ImportGui* igui)
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

        kill:
        SDL_CloseIO(sram);
    }
    SDL_UnlockMutex(igui->Mutex);
}

// note: this is probably not a standard meant to be stable, but w/e.
// we can work on a proper standard later.
void ImportGui_ParseNDSTxt(ImportGui* igui)
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

void ImportGui_Loop(MainGUI* mgui, ImportGui* igui, const bool NDS)
{
    if (!igui->Show)
    {
        igui->NeedReset = true;
        return;
    }

    ImGui_SetNextWindowSize((ImVec2){340.0, 400.0}, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Import NDS Game Card", &igui->Show, 0))
        return ImGui_End();

    if (igui->NeedReset)
    {
        igui->Name[0] = '\0';
        igui->ROMPath[0] = '\0';
        igui->SRAMPath[0] = '\0';
        igui->TxtPath[0] = '\0';
        if (igui->ROMDat.FriendlyName != NULL)
            SDL_free(igui->ROMDat.FriendlyName);
        memset(&igui->ROMDat, 0, sizeof(ImportGui) - offsetof(ImportGui, ROMDat));
        igui->TxtDat.SRAMID = 0xFFFFFF;

        igui->NeedReset = false;
    }

    SelectFile(igui->ROMPath, "NDS ROM", "nds;nds.enc", 1, igui->Mutex, igui->ROMDirty);
    SelectFile(igui->SRAMPath, "NDS SRAM", "sav", 1, igui->Mutex, igui->SRAMDirty);
    SelectFile(igui->TxtPath, "GM9 Dump Info", "txt", 1, igui->Mutex, igui->TxtDirty);
    if (ImGui_Button("Infer from provided Files\n"))
    {
        ImportGui_ParseNDSROM(igui);
        ImportGui_ParseNDSSRAM(igui);
        ImportGui_ParseNDSTxt(igui);

        if (igui->ROMDat.FriendlyName != NULL)
        {
            free(igui->Name);
            igui->Name = strdup(igui->ROMDat.FriendlyName);
        }

        if (igui->TxtDat.ROMID != 0)
        {
            igui->ROMChipID = igui->TxtDat.ROMID;
            u8 szbyte = (igui->TxtDat.ROMID >> 8) & 0xFF;
            if (szbyte < 0x80)
            {
                szbyte+=1;
                if (stdc_count_ones(szbyte) != 1)
                {
                    printf("Unknown ROM ChipID size field: %08"PRIX8"\n", igui->TxtDat.ROMID);
                    goto romSizeError;
                }
                igui->ROMSize = stdc_trailing_zeros(szbyte);
            }
            else if (szbyte > 0xF0)
            {
                u32 sz = (0x100-szbyte) * MiB(256);
                if (stdc_count_ones(sz) != 1)
                {
                    printf("Unknown ROM ChipID size field: %08"PRIX8"\n", igui->TxtDat.ROMID);
                    goto romSizeError;
                }
                igui->ROMSize = stdc_trailing_zeros(sz) - 20;
            }
        }
        else
        {
            romSizeError:
            if (igui->ROMDat.Size != 0)
            {
                if (igui->ROMDat.Size < 20) igui->ROMSize = 0;
                else igui->ROMSize = igui->ROMDat.Size - 20;
            }
        }

        igui->SPIType = GameCard_SPIBus_DirectSRAM;

        if (igui->SRAMDat.Size != 0)
        {
            if (igui->SRAMDat.Size < 9) igui->SRAMChipSize = 0;
            else igui->SRAMChipSize = igui->SRAMDat.Size - 9;
        }

        if (igui->TxtDat.SRAMID == 0xFFFFFF)
        {
            if (igui->SRAMChipSize == 0)
            {
                igui->SRAMType = GameCard_SRAMChip_EEPROM9BitAddr;
            }
            else if (igui->SRAMChipSize <= 7)
            {
                igui->SRAMType = GameCard_SRAMChip_EEPROM16BitAddr;
            }
            else
            {
                igui->SRAMType = GameCard_SRAMChip_EEPROM24BitAddr;
            }
        }
        else
        {
            igui->SRAMType = GameCard_SRAMChip_Flash24BitAddr;
            igui->FlashChipID = igui->TxtDat.SRAMID;
        }
    }

    ImGui_InputTextMultilineEx("Game Title", igui->Name, strlen(igui->Name)+1, (ImVec2){0, 45}, ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_CallbackResize, FilePathTextCallback, &igui->Name);

    const char* const romsizes[] = {"1 MiB", "2 MiB", "4 MiB", "8 MiB", "16 MiB", "32 MiB", "64 MiB", "128 MiB", "256 MiB", "512 MiB"};
    ImGui_ComboChar("ROM Chip Size", &igui->ROMSize, romsizes, NDS ? countof(romsizes) : 6);

    ImGui_InputIntEx("ROM Chip ID", &igui->ROMChipID, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);

    const char* const spitype[] = {"None", "Direct SRAM Chip", "Infrared (HLE)"};
    ImGui_ComboChar("SPI Bus Type", &igui->SPIType, spitype, countof(spitype));

    ImGui_BeginDisabled(igui->SPIType == GameCard_SPIBus_None);
    const char* const sramchiptype[] = {"None", "Flash (Max: 16 MiB)", "EEPROM w/ 9 Bit Addr (Max: 512 Bytes)", "EEPROM w/ 16 Bit Addr (Max: 64 KiB)", "EEPROM w/ 24 Bit Addr (Max: 16 MiB)"};
    ImGui_ComboChar("SRAM Chip Type", &igui->SRAMType, sramchiptype, countof(sramchiptype));

    ImGui_BeginDisabled(igui->SRAMType == GameCard_SRAMChip_None);
    const char* const sramsizes[] = {"512 B", "1 KiB", "2 KiB", "4 KiB", "8 KiB", "16 KiB", "32 KiB", "64 KiB", "128 KiB", "256 KiB", "512 KiB", "1 MiB", "2 MiB", "4 MiB", "8 MiB", "16 MiB"};
    ImGui_ComboChar("SRAM Chip Size", &igui->SRAMChipSize, sramsizes, countof(sramsizes));


    ImGui_BeginDisabled(igui->SRAMType != GameCard_SRAMChip_Flash24BitAddr);
    ImGui_InputIntEx("Flash Chip ID", &igui->FlashChipID, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
    igui->FlashChipID &= 0xFFFFFF;

    ImGui_EndDisabled();
    ImGui_EndDisabled();
    ImGui_EndDisabled();

    if (ImGui_Button("Import") && (igui->ROMPath[0] != '\0'))
    {
        if (igui->ROMPath[0] == '\0')
            return ImGui_End();

        size_t sramsize = (size_t)1<<(igui->SRAMChipSize + 9);
        u8 srambuffer[sramsize];

        constexpr char cfgname[] = "/cfg.soup";
        constexpr char savfolder[] = "/sav";
        constexpr char savfile[] = "/sav.sav"; // TODO: backups
        constexpr char iconfile[] = "/icon.bin";
        constexpr char ndsfolder[] = "GameCard/";

        char* start; char* end;
        if ((start = strrchr(igui->ROMPath, '/')) == NULL)
            start = igui->ROMPath;
        else start+=1;
        if ((end = strrchr(start, '.')) == NULL)
            end = start+strlen(start)+1;

        char* romfoldername;
        if ((romfoldername = calloc(1, end-start+1)) == NULL)
            goto out;
        strncpy(romfoldername, start, end-start);
        char* path = SDL_GetPrefPath("DualSOUP", "DualSOUP");

        size_t basefolderlen = strlen(path)+sizeof(ndsfolder)+strlen(romfoldername);
        char submpath[basefolderlen+sizeof(cfgname)];
        char savpath[basefolderlen+sizeof(savfolder)+sizeof(savfile)];
        char iconpath[basefolderlen+sizeof(iconfile)];
        if (NDS)
        {
            strcpy(submpath, path);
            strcat(submpath, ndsfolder);
            strcat(submpath, romfoldername);
            strcpy(savpath, submpath);
            strcpy(iconpath, submpath);
            strcat(submpath, cfgname);
            strcat(iconpath, iconfile);
            strcat(savpath, savfolder);
            // make folder
            if (!SDL_CreateDirectory(savpath))
            {
                printf("Directory creation failure: %s\n", SDL_GetError());
                goto exit;
            }
            strcat(savpath, savfile);
        }
        else
        {
            constexpr char gbafolder[] = "GamePak/";
            //submpath = malloc(strlen(path)+sizeof(gbafolder)+strlen(igui->Name));
            strcpy(submpath, path);
            strcat(submpath, gbafolder);
            strcat(submpath, igui->Name);
        }
        printf("%s\n%s\n", submpath, savpath);

        GameCardConfig cfg = {
            .ROMBusType = GameCard_ROMBus_Standard,
            .ROMChipSize = igui->ROMSize + 20,
            .ROMPaddingByte = 0xFF,
            .ROMChipID = igui->ROMChipID,
            .ROMPath = igui->ROMPath,
            .SPIBusType = igui->SPIType,
            .SRAMChipType = igui->SRAMType,
            .SRAMChipSize = igui->SRAMChipSize + 9,
            .FlashChipID = igui->FlashChipID,
            .SRAMPath = savpath,
            .ImportKey1FromNTRBios7 = true,
            .ManualKey1Path = nullptr,
            .FriendlyName = igui->Name,
        };

        Config_Write(submpath, &cfg, GameCardCfgData, countof(GameCardCfgData), nullptr, igui->Mutex);

        if ((igui->SPIType == GameCard_SPIBus_None) || (igui->SRAMType == GameCard_SRAMChip_None)) // exit if no sram
            goto skipsav;
        memset(srambuffer, 0xFF, sramsize);

        // load provided save
        if (igui->SRAMPath[0] != '\0')
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
        exit:
        SDL_free(path);
        // refresh library list
        LibraryGui_InitList(mgui);
    }
    out:
    ImGui_End();
}
