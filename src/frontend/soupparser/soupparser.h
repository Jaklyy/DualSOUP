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
    char** EnumNames;
    size_t Offset;
    SOUPParserTypes Type;
    u8 MaxEnum;
    u8 Default;
} ConfigEntry;

static const ConfigEntry SystemCfg[] =
{
    {
        .Name=      "NTRAudioOut",
        .EnumNames= (char*[]){"10bit", "16bit"},
        .Offset=    offsetof(SysCfg, NTRAudioOut),
        .Type=      SEARCH_ENUMU8,
        .MaxEnum=   2,
        .Default=   NTRAudioOut_10,
    },
    {
        .Name=      "NTRPowMan",
        .EnumNames= (char*[]){"NTR", "USG", "TWL"},
        .Offset=    offsetof(SysCfg, NTRPowMan),
        .Type=      SEARCH_ENUMU8,
        .MaxEnum=   3,
        .Default=   NTRPowMan_USG,
    },
    {
        .Name=      "WiFiNVRAMSize",
        .EnumNames= (char*[]){"256KiB", "512KiB", "128KiB", "4KiB"},
        .Offset=    offsetof(SysCfg, WiFiNVRAMSize),
        .Type=      SEARCH_ENUMU8,
        .MaxEnum=   4,
        .Default=   WiFiNVRAM_256KiB,
    },
    {
        .Name=      "WiFiNVRAMWriteProt",
        .EnumNames= (char*[]){"Enable", "Disable"},
        .Offset=    offsetof(SysCfg, WiFiNVRAMWriteProt),
        .Type=      SEARCH_ENUMU8,
        .MaxEnum=   3,
        .Default=   WiFiNVRAMWriteProt_Enabled,
    },
};

static const ConfigEntry MainCfg[] =
{
#if 0
    {
        .Name=      "ConsoleModel",
        .EnumNames= (char*[]){"Custom, USG"},
        .Offset=    offsetof(CoreCfg, Model),
        .Type=      SEARCH_ENUMU8,
        .MaxEnum=   2,
        .Default=   ConsoleModel_USG,
    },
    {
        .Name=      "CustomModelPath",
        .Offset=    offsetof(CoreCfg, CustomModel),
        .Type=      SEARCH_STRING,
    },
#endif
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
        .Name=      "WiFiNVRAMPath",
        .Offset=    offsetof(CoreCfg, NTR.NVRAM),
        .Type=      SEARCH_STRING,
    },
};

FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode);
bool SOUPParser(const char* haystack, const char* needle, const char* cmpstr, const u8 type, void* ret);
void Config_Write(const char* path, void* cfgin, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex* mutex);
void Config_Load(const char* path, void* cfgout, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex** mutex);
