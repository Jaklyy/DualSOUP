#pragma once
#include <stdio.h>
#include "frontend/gui/maingui.h"
#include "core/utils.h"




typedef enum : u8
{
    SEARCH_NULL,

    SEARCH_S8DEC,
    SEARCH_U8DEC,
    SEARCH_U8HEX,
    SEARCH_ENUMU8,

    SEARCH_S16DEC,
    SEARCH_U16DEC,
    SEARCH_U16HEX,

    SEARCH_S32DEC,
    SEARCH_U32DEC,
    SEARCH_U32HEX,

    SEARCH_S64DEC,
    SEARCH_U64DEC,
    SEARCH_U64HEX,

    SEARCH_BOOL,
    SEARCH_INT,
    SEARCH_ENUMINT,
    SEARCH_FLOAT,

    SEARCH_STRING,
    SEARCH_EXISTS,
} SOUPParserTypes;

static const char* boolnames[] = {"Enabled", "Disabled"};

typedef struct
{
    const char* Name;
    const char** EnumNames;
    size_t Offset;
    union {u64 UDefVal; s64 SDefVal; double FDefVal; bool BDefVal;};
    union {u64 UMaxVal; s64 SMaxVal; double FMaxVal;};
    union {u64 UMinVal; s64 SMinVal; double FMinVal;};
    SOUPParserTypes Type;
} ConfigEntry;

// cfg used to control the GUI state (NOTE: a lot of state is held in the imgui.ini file)
typedef struct
{
    int NumDisplayWindows;
    int MainWinWidth;
    int MainWinHeight;
    int MainWinX;
    int MainWinY;
    bool MainWinMaximized;
    DisplayWindow DisplayWindow[GUI_MaxDisplayWindows];
    char* LibrarySel[3];
} GuiCfg;

 // this is declared differently because otherwise i can't figure out how to forward declare this...
typedef struct MainCfg
{
    SDL_Mutex* Mutex; // make sure these values aren't being messed with before reading.
    bool Dirty; // update the fecking config file
    CoreCfg CoreCfg;
    GuiCfg GuiCfg;
    u64 LoggingMask;
} MainCfg;

#include "core/carts/gamecard.h"

static const ConfigEntry GameCardCfgData[] =
{
    {
        .Name=      "ROMBusType",
        .EnumNames= (const char*[]){
            [GameCard_ROMBus_Ejected]="Ejected",
            [GameCard_ROMBus_Standard]="Standard",
        },
        .Offset=    offsetof(GameCardCfg, ROMBusType),
        .UDefVal=   GameCard_ROMBus_Standard,
        .UMinVal=   0,
        .UMaxVal=   GameCard_ROMBus_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "ROMChipSize",
        .Offset=    offsetof(GameCardCfg, ROMChipSize),
        .SDefVal=   20,
        .SMinVal=   20,
        .SMaxVal=   32, // standard card protocol doesn't support >32 bit addresses
        .Type=      SEARCH_U8DEC,
    },
    {
        .Name=      "ROMPaddingByte", // TODO: consider allowing multi-byte patterns?
        .Offset=    offsetof(GameCardCfg, ROMPaddingByte),
        .UDefVal=   0xFF,
        .UMinVal=   0,
        .UMaxVal=   0xFF,
        .Type=      SEARCH_U8HEX,
    },
    {
        .Name=      "ROMChipID",
        .Offset=    offsetof(GameCardCfg, ROMChipID),
        .UDefVal=   DefaultChipID,
        .UMinVal=   0x00000000, // NOTE: should i allow the user to submit an 0x0 value? supposedly games dont like that...
        .UMaxVal=   0xFFFFFFFF,
        .Type=      SEARCH_U32HEX,
    },
    {
        .Name=      "ROMPath",
        .Offset=    offsetof(GameCardCfg, ROMPath),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "SPIBusType",
        .EnumNames= (const char*[]){
            [GameCard_SPIBus_None]="None",
            [GameCard_SPIBus_DirectSRAM]="DirectSRAM",
            [GameCard_SPIBus_InfraredHLE]="InfraredHLE",
        },
        .Offset=    offsetof(GameCardCfg, SPIBusType),
        .UDefVal=   GameCard_SPIBus_DirectSRAM,
        .UMinVal=   0,
        .UMaxVal=   GameCard_SPIBus_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "SRAMChipType",
        .EnumNames= (const char*[]){
            [GameCard_SRAMChip_None]="None",
            [GameCard_SRAMChip_Flash24BitAddr]="Flash24",
            [GameCard_SRAMChip_EEPROM9BitAddr]="EEPROM9",
            [GameCard_SRAMChip_EEPROM16BitAddr]="EEPROM16",
            [GameCard_SRAMChip_EEPROM24BitAddr]="EEPROM24",
        },
        .Offset=    offsetof(GameCardCfg, SRAMChipType),
        .UDefVal=   GameCard_SRAMChip_None,
        .UMinVal=   0,
        .UMaxVal=   GameCard_SRAMChip_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "SRAMChipSize",
        .Offset=    offsetof(GameCardCfg, SRAMChipSize),
        .UDefVal=   9,
        .UMinVal=   9,
        .UMaxVal=   24, // standard sram chips dont seem to support > 24 bit address indexing
        .Type=      SEARCH_U8DEC,
    },
    {
        .Name=      "FlashChipID",
        .Offset=    offsetof(GameCardCfg, FlashChipID),
        .UDefVal=   DefaultFlashID,
        .UMinVal=   0x000000,
        .UMaxVal=   0xFFFFFF,
        .Type=      SEARCH_U32HEX,
    },
    {
        .Name=      "Key1FromBios",
        .Offset=    offsetof(GameCardCfg, ImportKey1FromNTRBios7),
        .BDefVal=   true,
        .Type=      SEARCH_BOOL,
    },
    {
        .Name=      "ManualKey1Path",
        .Offset=    offsetof(GameCardCfg, ManualKey1Path),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "FriendlyName",
        .Offset=    offsetof(GameCardCfg, FriendlyName),
        .Type=      SEARCH_STRING,
    },
};

static const ConfigEntry GamePakCfgData[] =
{
    {
        .Name=      "ROMPath",
        .Offset=    offsetof(GamePakCfg, ROMPath),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "ROMChipSize",
        .Offset=    offsetof(GamePakCfg, ROMChipSize),
        .SDefVal=   20,
        .SMinVal=   20,
        .SMaxVal=   25, // TODO: 64 MiB carts
        .Type=      SEARCH_U8DEC,
    },
    {
        .Name=      "ROMPaddingByte", // TODO: consider allowing multi-byte patterns?
        .Offset=    offsetof(GamePakCfg, ROMPaddingByte),
        .UDefVal=   0xFF,
        .UMinVal=   0,
        .UMaxVal=   0xFF,
        .Type=      SEARCH_U8HEX,
    },
    {
        .Name=      "SRAMType",
        .Offset=    offsetof(GamePakCfg, SRAMType),
        .EnumNames= (const char*[]){
            [GamePakSRAM_None]="None",
            [GamePakSRAM_SRAM]="SRAM",
            [GamePakSRAM_Flash]="Flash",
        },
        .UDefVal=   GamePakSRAM_None,
        .UMinVal=   0,
        .UMaxVal=   GamePakSRAM_Max,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "FlashType",
        .Offset=    offsetof(GamePakCfg, GamePakFlashType),
        .EnumNames= (const char*[]){
            [GamePakFlash_Atmel]="Atmel",
            [GamePakFlash_SST]="SST",
            [GamePakFlash_Panasonic]="Panasonic",
            [GamePakFlash_Macronix]="Macronix",
            [GamePakFlash_Sanyo]="Sanyo",
        },
        .UDefVal=   GamePakFlash_Macronix,
        .UMinVal=   0,
        .UMaxVal=   GamePakFlash_Max,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "SRAMChipSize",
        .Offset=    offsetof(GamePakCfg, SRAMChipSize),
        .UDefVal=   9,
        .UMinVal=   9,
        .UMaxVal=   17, // idk
        .Type=      SEARCH_U8DEC,
    },
    {
        .Name=      "FlashChipID",
        .Offset=    offsetof(GamePakCfg, FlashChipID),
        .UDefVal=   0x0000,
        .UMinVal=   0x0000,
        .UMaxVal=   0xFFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "FriendlyName",
        .Offset=    offsetof(GamePakCfg, FriendlyName),
        .Type=      SEARCH_STRING,
    },
};

static const ConfigEntry SysCfgData[] =
{
    {
        .Name=      "NTRAudioOut",
        .EnumNames= (const char*[]){
            [NTRAudioOut_10]="10bit",
            [NTRAudioOut_16]="16bit",
        },
        .Offset=    offsetof(SysCfg, NTRAudioOut),
        .UDefVal=   NTRAudioOut_10,
        .UMinVal=   0,
        .UMaxVal=   NTRAudioOut_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "NTRPMIC",
        .EnumNames= (const char*[]){
            [NTRPMIC_NTR]="NTR",
            [NTRPMIC_USG]="USG",
            [NTRPMIC_TWL]="TWL",
        },
        .Offset=    offsetof(SysCfg, NTRPMIC),
        .UDefVal=   NTRPMIC_USG,
        .UMinVal=   0,
        .UMaxVal=   NTRPMIC_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "WiFiNVRAMSize",
        .EnumNames= (const char*[]){
            [WiFiNVRAM_256KiB]="256KiB",
            [WiFiNVRAM_512KiB]="512KiB",
            [WiFiNVRAM_128KiB]="128KiB",
            [WiFiNVRAM_4KiB]="4KiB",
        },
        .Offset=    offsetof(SysCfg, WiFiNVRAMSize),
        .UDefVal=   WiFiNVRAM_256KiB,
        .UMinVal=   0,
        .UMaxVal=   WiFiNVRAM_MAX,
        .Type=      SEARCH_ENUMU8,
    },
};

static const ConfigEntry CoreCfgData[] =
{
    {
        .Name=      "TSCLeftMax",
        .Offset=    offsetof(CoreCfg, TSC.L),
        .UDefVal=   0,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "TSCRightMax",
        .Offset=    offsetof(CoreCfg, TSC.R),
        .UDefVal=   0xFFF,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "TSCTopMax",
        .Offset=    offsetof(CoreCfg, TSC.T),
        .UDefVal=   0,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "TSCBottomMax",
        .Offset=    offsetof(CoreCfg, TSC.B),
        .UDefVal=   0xBFF,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "ConsoleModel",
        .EnumNames= (const char*[]){
            [ConsoleModel_Custom]="Custom",
            [ConsoleModel_NTR001]="NTR001",
            [ConsoleModel_USG001]="USG001",
        },
        .Offset=    offsetof(CoreCfg, Model),
        .UMinVal=   0,
        .UMaxVal=   ConsoleModel_TWL001,
        .UDefVal=   ConsoleModel_USG001,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "NTRBios7Path",
        .Offset=    offsetof(CoreCfg, NTR.Bios7),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "NTRBios9Path",
        .Offset=    offsetof(CoreCfg, NTR.Bios9),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "AGBBios7Path",
        .Offset=    offsetof(CoreCfg, AGB.Bios7),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "FriendlyName",
        .Offset=    offsetof(CoreCfg, FriendlyName),
        .Type=      SEARCH_STRING,
    },
};

constexpr ImVec2 GUI_DisplayOffsetDefaults[] = {{0.0, 0.0}, {0.0, 192.0}, {256.0, 0.0}, {256.0, 192.0}};
static_assert(countof(GUI_DisplayOffsetDefaults) >= GUI_MaxDisplaysPerWindow);

#define PERDISPLAYSETTINGS(winnum, dispnum) \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_OffsetX", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Pos.x), \
        .FDefVal=   GUI_DisplayOffsetDefaults[dispnum].x, \
        .FMinVal=   GUI_MinDisplayPosX, \
        .FMaxVal=   GUI_MaxDisplayPosX, \
        .Type=      SEARCH_FLOAT, \
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_OffsetY", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Pos.y), \
        .FDefVal=   GUI_DisplayOffsetDefaults[dispnum].y, \
        .FMinVal=   GUI_MinDisplayPosY, \
        .FMaxVal=   GUI_MaxDisplayPosY, \
        .Type=      SEARCH_FLOAT, \
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_SizeX", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Sz.x), \
        .FDefVal=   256.0, \
        .FMinVal=   GUI_MinDisplayWidth, \
        .FMaxVal=   GUI_MaxDisplayWidth, \
        .Type=      SEARCH_FLOAT, \
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_SizeY", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Sz.y), \
        .FDefVal=   192.0, \
        .FMinVal=   GUI_MinDisplayHeight, \
        .FMaxVal=   GUI_MaxDisplayHeight, \
        .Type=      SEARCH_FLOAT, \
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_IsBottomScreen", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Bottom), \
        .BDefVal=   (dispnum&1), \
        .Type=      SEARCH_BOOL, \
    },

#define PERDISPLAYWINDOWSETTINGS(num) \
    { \
        .Name =     "DispWin"#num"_LockPos", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[num].LockPos), \
        .BDefVal=   false, \
        .Type=      SEARCH_BOOL, \
    }, \
    { \
        .Name =     "DispWin"#num"_NoDecor", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[num].NoDecor), \
        .BDefVal=   false, \
        .Type=      SEARCH_BOOL, \
    }, \
    { \
        .Name =     "DispWin"#num"_ScaleMode", \
        .EnumNames= (const char*[]){ \
            [DispWinScaleMode_NoScale]="NoScale", \
            [DispWinScaleMode_Stretch]="Stretch", \
            [DispWinScaleMode_PreserveAspectRatio]="PreserveAspectRatio", \
            [DispWinScaleMode_Integer]="Integer" \
        }, \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[num].ScaleMode), \
        .UDefVal=   DispWinScaleMode_PreserveAspectRatio, \
        .SMinVal=   0, \
        .SMaxVal=   DispWinScaleMode_MAX, \
        .Type=      SEARCH_ENUMINT, \
    }, \
    { \
        .Name =     "DispWin"#num"_NumDisplays", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[num].NumDisplays), \
        .SDefVal=   2, \
        .SMinVal=   GUI_MinDisplaysPerWindow, \
        .SMaxVal=   GUI_MaxDisplaysPerWindow, \
        .Type=      SEARCH_INT, \
    }, \
    PERDISPLAYSETTINGS(num, 0) \
    PERDISPLAYSETTINGS(num, 1) \
    PERDISPLAYSETTINGS(num, 2) \
    PERDISPLAYSETTINGS(num, 3)

static_assert(GUI_MaxDisplayWindows <= 4);
static_assert(GUI_MaxDisplaysPerWindow <= 4);

constexpr int menubarheight = 19; // todo: un-hardcode

static const ConfigEntry MainCfgData[] =
{
    {
        .Name=      "MainWinMaximized",
        .Offset=    offsetof(MainCfg, GuiCfg.MainWinMaximized),
        .BDefVal=   false,
        .Type=      SEARCH_BOOL,
    },
    {
        .Name=      "MainWinWidth",
        .Offset=    offsetof(MainCfg, GuiCfg.MainWinWidth),
        .SDefVal=   256*2,
        .SMinVal=   32,
        .SMaxVal=   INT_MAX,
        .Type=      SEARCH_INT,
    },
    {
        .Name=      "MainWinHeight",
        .Offset=    offsetof(MainCfg, GuiCfg.MainWinHeight),
        .SDefVal=   (192*2*2) + menubarheight,
        .SMinVal=   32,
        .SMaxVal=   INT_MAX,
        .Type=      SEARCH_INT,
    },
    {
        .Name=      "MainWinX",
        .Offset=    offsetof(MainCfg, GuiCfg.MainWinX),
        .SDefVal=   -1,
        .SMinVal=   -1,
        .SMaxVal=   INT_MAX,
        .Type=      SEARCH_INT,
    },
    {
        .Name=      "MainWinY",
        .Offset=    offsetof(MainCfg, GuiCfg.MainWinY),
        .SDefVal=   -1,
        .SMinVal=   -1,
        .SMaxVal=   INT_MAX,
        .Type=      SEARCH_INT,
    },
    {
        .Name=      "NumDisplayWindows",
        .Offset=    offsetof(MainCfg, GuiCfg.NumDisplayWindows),
        .SDefVal=   1,
        .SMinVal=   GUI_MinDisplayWindows,
        .SMaxVal=   GUI_MaxDisplayWindows,
        .Type=      SEARCH_INT,
    },
    {
        .Name=      "SysLibrarySelected",
        .Offset=    offsetof(MainCfg, GuiCfg.LibrarySel[LibType_Sys]),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "NDSLibrarySelected",
        .Offset=    offsetof(MainCfg, GuiCfg.LibrarySel[LibType_NDS]),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "GBALibrarySelected",
        .Offset=    offsetof(MainCfg, GuiCfg.LibrarySel[LibType_GBA]),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "LoggingMask",
        .Offset=    offsetof(MainCfg, LoggingMask),
        .UDefVal=   0,
        .UMinVal=   0,
        .UMaxVal=   u64_max,
        .Type=      SEARCH_U64HEX,
    },
    PERDISPLAYWINDOWSETTINGS(0)
    PERDISPLAYWINDOWSETTINGS(1)
    PERDISPLAYWINDOWSETTINGS(2)
    PERDISPLAYWINDOWSETTINGS(3)
};

#undef PERDISPLAYWINDOWSETTINGS
#undef PERDISPLAYSETTINGS

void Config_Write(const char* path, void* cfgin, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex* mutex);
void Config_Load(const char* path, void* cfgout, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex** mutex);
void Config_Free(void* cfgin, const ConfigEntry* cfgref, const size_t cfgnum);
