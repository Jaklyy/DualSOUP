#pragma once
#include <stdio.h>
#include "../../core/utils.h"




typedef enum : u8
{
    SEARCH_S32DEC,
    SEARCH_U32DEC,
    SEARCH_U32HEX,
    SEARCH_S64DEC,
    SEARCH_U64DEC,
    SEARCH_U64HEX,
    SEARCH_STRING,
    SEARCH_EXISTS,
    SEARCH_ENUMU8,
} SOUPParserTypes;

typedef struct
{
    char* Name;
    size_t Offset;
    SOUPParserTypes Type;
    u8 MaxNum;
    char** EnumNames;
    u8 Default;
} ConfigEntry;

static const ConfigEntry MainCfg[] =
{
    {
        "NTRAudioOut",
        offsetof(CoreCfg, SysCfg.NTRAudioOut),
        SEARCH_ENUMU8,
        2,
        (char*[]){"10bit", "16bit"},
        NTRAudioOut_10,
    },
    {
        "NTRPowMan",
        offsetof(CoreCfg, SysCfg.NTRPowMan),
        SEARCH_ENUMU8,
        3,
        (char*[]){"NTR", "USG", "TWL"},
        NTRPowMan_USG,
    },
    {
        "WiFiNVRAMSize",
        offsetof(CoreCfg, SysCfg.WiFiNVRAMSize),
        SEARCH_ENUMU8,
        4,
        (char*[]){"256KiB", "512KiB", "128KiB", "4KiB"},
        WiFiNVRAM_256KiB,
    },
    {
        "WiFiNVRAMWriteProt",
        offsetof(CoreCfg, SysCfg.WiFiNVRAMWriteProt),
        SEARCH_ENUMU8,
        3,
        (char*[]){"Enable", "Disable"},
        WiFiNVRAMWriteProt_Enabled,
    },
    {
        "NTRBios7Path",
        offsetof(CoreCfg, NTR.Bios7),
        SEARCH_STRING,
        1,
        nullptr,
        0,
    },
    {
        "NTRBios9Path",
        offsetof(CoreCfg, NTR.Bios9),
        SEARCH_STRING,
        1,
        nullptr,
        0,
    },
    {
        "WiFiNVRAMPath",
        offsetof(CoreCfg, NTR.NVRAM),
        SEARCH_STRING,
        1,
        nullptr,
        0,
    },
};


constexpr char CfgStr_WiFiNVRAMSize[][14] = {"WiFiNVRAMSize", "4MiB", "8MiB", "16MiB", "32MiB"};
constexpr char CfgStr_WiFiNVRAMWriteProt[][19] = {"WiFiNVRAMWriteProt", "Enabled", "Disabled"};
constexpr char CfgStr_NTRBios7[] = "NTRBios7Path";
constexpr char CfgStr_NTRBios9[] = "NTRBios9Path";
constexpr char CfgStr_WiFiNVRAM[] = "WiFiNVRAMPath";

FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode);
bool SOUPParser(const char* haystack, const char* needle, const char* cmpstr, const u8 type, void* ret);
void Config_Write(const char* path, CoreCfg* corecfg);
CoreCfg Config_Load(const char* path);
