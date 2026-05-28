#pragma once
#include "../utils.h"



struct Console;

enum EncryptMode : u8
{
    Unenc,
    Key1,
    Key2,
};

typedef enum : u8
{
    Gamecard_ROMBus_Standard,
    //Gamecard_ROMBus_NAND,
    //Gamecard_ROMBus_FlashcartGeneric,

    Gamecard_ROMBus_MAX [[maybe_unused]],
} Gamecard_ROMBus;

typedef enum : u8
{
    Gamecard_SPIBus_None,
    Gamecard_SPIBus_DirectSRAM,
    Gamecard_SPIBus_InfraredHLE,
    //Gamecard_SPIBus_InfraredLLE,
    //Gamecard_SPIBus_BluetoothHLE,
    //Gamecard_SPIBus_BluetoothLLE,

    Gamecard_SPIBus_MAX [[maybe_unused]],
} Gamecard_SPIBus;

typedef enum : u8
{
    Gamecard_SRAMChip_None,
    Gamecard_SRAMChip_Flash24BitAddr,
    Gamecard_SRAMChip_EEPROM9BitAddr,
    Gamecard_SRAMChip_EEPROM16BitAddr,
    Gamecard_SRAMChip_EEPROM24BitAddr,
    //Gamecard_SRAMChip_FRAM9BitAddr,
    //Gamecard_SRAMChip_FRAM16BitAddr,
    //Gamecard_SRAMChip_FRAM24BitAddr,

    Gamecard_SRAMChip_MAX [[maybe_unused]],
} Gamecard_SRAMChip;

constexpr u32 DefaultChipID  = 0x04030201;
constexpr u32 DefaultFlashID = 0x030201;

typedef struct
{
    Gamecard_ROMBus ROMBusType;
    u8 ROMChipSize;
    u8 ROMPaddingByte;
    u32 ROMChipID;
    char* ROMPath;

    Gamecard_SPIBus SPIBusType;
    Gamecard_SRAMChip SRAMChipType;
    u8 SRAMChipSize;
    u32 FlashChipID;

    bool ImportKey1FromNTRBios7;
    char* ManualKey1Path;
} GamecardConfig;

typedef struct
{
    u8 Mode;
    bool Buffered;
    void* (*CmdHandler) (struct Console*, bool);
    u32 (*ReadHandler) (void*);
    u32 Address;
    s32 NumWords;
    u32 RomSize;
    u32 ChipID;
    u32 WordBuffer;
    u32* ROM;
    u8 (*SPI_CMDSend)(void*, const u8, const bool);
    void (*SPI_Cleanup)(void*);
    void* SPI;
    u32 Key1[4168/sizeof(u32)];
} Gamecard;


bool Gamecard_Init(Gamecard* card, const char* romname, u8* bios7);
void Gamecard_Cleanup(Gamecard* card);
u32 Gamecard_ROMDataRead(struct Console* sys, timestamp cur, const bool a9);
u32 Gamecard_IOReadHandler(struct Console* sys, u32 addr, const bool a9);
void Gamecard_IOWriteHandler(struct Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9);
