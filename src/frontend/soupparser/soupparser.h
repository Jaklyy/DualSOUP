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
    DisplayWindow DisplayWindow[GUI_MaxDisplayWindows];
} GuiCfg;

 // this is declared differently because otherwise i can't figure out how to forward declare this...
typedef struct MainCfg
{
    SDL_Mutex* Mutex; // make sure these values aren't being messed with before reading.
    bool Dirty; // update the fecking config file
    CoreCfg CoreCfg;
    GuiCfg GuiCfg;
} MainCfg;

#include "core/carts/gamecard.h"

static const ConfigEntry GameCardCfgData[] =
{
    {
        .Name=      "ROMBusType",
        .EnumNames= (const char*[]){"Standard"},
        .Offset=    offsetof(GameCardConfig, ROMBusType),
        .UDefVal=   GameCard_ROMBus_Standard,
        .UMinVal=   0,
        .UMaxVal=   GameCard_ROMBus_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "ROMChipSize",
        .Offset=    offsetof(GameCardConfig, ROMChipSize),
        .UDefVal=   -1, // auto
        .UMinVal=   0,
        .UMaxVal=   32, // standard card protocol doesn't support >32 bit addresses
        .Type=      SEARCH_S8DEC,
    },
    {
        .Name=      "ROMPaddingByte", // TODO: consider allowing multi-byte patterns?
        .Offset=    offsetof(GameCardConfig, ROMPaddingByte),
        .UDefVal=   0xFF,
        .UMinVal=   0,
        .UMaxVal=   0xFF,
        .Type=      SEARCH_U8HEX,
    },
    {
        .Name=      "ROMChipID",
        .Offset=    offsetof(GameCardConfig, ROMChipID),
        .UDefVal=   DefaultChipID,
        .UMinVal=   0x00000000, // NOTE: should i allow the user to submit an 0x0 value? supposedly games dont like that...
        .UMaxVal=   0xFFFFFFFF,
        .Type=      SEARCH_U32HEX,
    },
    {
        .Name=      "ROMPath",
        .Offset=    offsetof(GameCardConfig, ROMPath),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "SPIBusType",
        .EnumNames= (const char*[]){"None", "DirectSRAM", "InfraredHLE"},
        .Offset=    offsetof(GameCardConfig, SPIBusType),
        .UDefVal=   GameCard_SPIBus_DirectSRAM,
        .UMinVal=   0,
        .UMaxVal=   GameCard_SPIBus_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "SRAMChipType",
        .EnumNames= (const char*[]){"None", "Flash24", "EEPROM9", "EEPROM16", "EEPROM24"},
        .Offset=    offsetof(GameCardConfig, SRAMChipType),
        .UDefVal=   GameCard_SRAMChip_None,
        .UMinVal=   0,
        .UMaxVal=   GameCard_SRAMChip_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "SRAMChipSize",
        .Offset=    offsetof(GameCardConfig, SRAMChipSize),
        .UDefVal=   -1, // auto
        .UMinVal=   0,
        .UMaxVal=   24, // standard sram chips dont seem to support > 24 bit address indexing
        .Type=      SEARCH_U8DEC,
    },
    {
        .Name=      "FlashChipID",
        .Offset=    offsetof(GameCardConfig, FlashChipID),
        .UDefVal=   DefaultFlashID,
        .UMinVal=   0x000000,
        .UMaxVal=   0xFFFFFF,
        .Type=      SEARCH_U32HEX,
    },
    {
        .Name=      "Key1FromBios",
        .Offset=    offsetof(GameCardConfig, FlashChipID),
        .BDefVal=   true,
        .Type=      SEARCH_BOOL,
    },
    {
        .Name=      "ManualKey1Path",
        .Offset=    offsetof(GameCardConfig, ManualKey1Path),
        .Type=      SEARCH_STRING,
    },
};

static const ConfigEntry SystemCfgData[] =
{
    {
        .Name=      "NTRAudioOut",
        .EnumNames= (const char*[]){"10bit", "16bit"},
        .Offset=    offsetof(SysCfg, NTRAudioOut),
        .UDefVal=   NTRAudioOut_10,
        .UMinVal=   0,
        .UMaxVal=   NTRAudioOut_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "NTRPMIC",
        .EnumNames= (const char*[]){"NTR", "USG", "TWL"},
        .Offset=    offsetof(SysCfg, NTRPMIC),
        .UDefVal=   NTRPMIC_USG,
        .UMinVal=   0,
        .UMaxVal=   NTRPMIC_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "WiFiNVRAMSize",
        .EnumNames= (const char*[]){"256KiB", "512KiB", "128KiB", "4KiB"},
        .Offset=    offsetof(SysCfg, WiFiNVRAMSize),
        .UDefVal=   WiFiNVRAM_256KiB,
        .UMinVal=   0,
        .UMaxVal=   WiFiNVRAM_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "WiFiNVRAMWriteProt",
        .EnumNames= (const char*[]){"Enable", "Disable"},
        .Offset=    offsetof(SysCfg, WiFiNVRAMWriteProt),
        .UDefVal=   WiFiNVRAMWriteProt_Enabled,
        .UMinVal=   0,
        .UMaxVal=   WiFiNVRAMWriteProt_MAX,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "TSCLeftMax",
        .Offset=    offsetof(SysCfg, TSCL),
        .UDefVal=   0,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "TSCRightMax",
        .Offset=    offsetof(SysCfg, TSCR),
        .UDefVal=   0xFFF,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "TSCTopMax",
        .Offset=    offsetof(SysCfg, TSCT),
        .UDefVal=   0,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
    },
    {
        .Name=      "TSCBottomMax",
        .Offset=    offsetof(SysCfg, TSCB),
        .UDefVal=   0xBFF,
        .UMinVal=   0,
        .UMaxVal=   0xFFF,
        .Type=      SEARCH_U16HEX,
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
        .Type=      SEARCH_FLOAT,\
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_OffsetY", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Pos.y), \
        .FDefVal=   GUI_DisplayOffsetDefaults[dispnum].y, \
        .FMinVal=   GUI_MinDisplayPosY, \
        .FMaxVal=   GUI_MaxDisplayPosY, \
        .Type=      SEARCH_FLOAT,\
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_SizeX", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Sz.x), \
        .FDefVal=   256.0, \
        .FMinVal=   GUI_MinDisplayWidth, \
        .FMaxVal=   GUI_MaxDisplayWidth, \
        .Type=      SEARCH_FLOAT,\
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_SizeY", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Sz.y), \
        .FDefVal=   192.0, \
        .FMinVal=   GUI_MinDisplayHeight, \
        .FMaxVal=   GUI_MaxDisplayHeight, \
        .Type=      SEARCH_FLOAT,\
    }, \
    { \
        .Name=      "DispWin"#winnum"_Disp"#dispnum"_IsBottomScreen", \
        .Offset=    offsetof(MainCfg, GuiCfg.DisplayWindow[winnum].Display[dispnum].Bottom), \
        .BDefVal=   (dispnum&1), \
        .Type=      SEARCH_BOOL,\
    }, \

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
        .EnumNames= (const char*[]){"NoScale", "Stretch", "PreserveAspectRatio", "Integer"}, \
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

static const ConfigEntry MainCfgData[] =
{
    // todo move this junk to SystemCfg
#if 0
    {
        .Name=      "ConsoleModel",
        .EnumNames= (char*[]){"Custom, USG"},
        .Offset=    offsetof(CoreCfg, Model),
        .UMinVal=   0,
        .UMaxVal=   2,
        .UDefVal=   ConsoleModel_USG,
        .Type=      SEARCH_ENUMU8,
    },
    {
        .Name=      "CustomModelPath",
        .Offset=    offsetof(CoreCfg, CustomModel),
        .Type=      SEARCH_STRING,
    },
#endif
    {
        .Name=      "NTRBios7Path",
        .Offset=    offsetof(MainCfg, CoreCfg.NTR.Bios7),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "NTRBios9Path",
        .Offset=    offsetof(MainCfg, CoreCfg.NTR.Bios9),
        .Type=      SEARCH_STRING,
    },
    {
        .Name=      "WiFiNVRAMPath",
        .Offset=    offsetof(MainCfg, CoreCfg.NTR.NVRAM),
        .Type=      SEARCH_STRING,
    },
    {
        .Name =     "NumDisplayWindows",
        .Offset=    offsetof(MainCfg, GuiCfg.NumDisplayWindows),
        .SDefVal=   1,
        .SMinVal=   GUI_MinDisplayWindows,
        .SMaxVal=   GUI_MaxDisplayWindows,
        .Type=      SEARCH_INT,
    },
    PERDISPLAYWINDOWSETTINGS(0)
    PERDISPLAYWINDOWSETTINGS(1)
    PERDISPLAYWINDOWSETTINGS(2)
    PERDISPLAYWINDOWSETTINGS(3)
};

#undef PERDISPLAYWINDOWSETTINGS
#undef PERDISPLAYSETTINGS

FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode);
bool SOUPParser(const char* haystack, const char* needle, const char* cmpstr, const u8 type, void* ret);
void Config_Write(const char* path, void* cfgin, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex* mutex);
void Config_Load(const char* path, void* cfgout, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex** mutex);
