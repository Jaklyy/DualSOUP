#pragma once
#include "core/utils.h"



typedef struct Console Console;

enum EncryptMode : u8
{
    Unenc,
    Key1,
    Key2,
};

typedef enum : u8
{
    GameCard_ROMBus_Standard,
    //GameCard_ROMBus_NAND,
    //GameCard_ROMBus_FlashcardGeneric,

    GameCard_ROMBus_MAX [[maybe_unused]],
} GameCard_ROMBus;

typedef enum : u8
{
    GameCard_SPIBus_None,
    GameCard_SPIBus_DirectSRAM,
    GameCard_SPIBus_InfraredHLE,
    //GameCard_SPIBus_InfraredLLE,
    //GameCard_SPIBus_BluetoothHLE,
    //GameCard_SPIBus_BluetoothLLE,

    GameCard_SPIBus_MAX [[maybe_unused]],
} GameCard_SPIBus;

typedef enum : u8
{
    GameCard_SRAMChip_None,
    GameCard_SRAMChip_Flash24BitAddr,
    GameCard_SRAMChip_EEPROM9BitAddr,
    GameCard_SRAMChip_EEPROM16BitAddr,
    GameCard_SRAMChip_EEPROM24BitAddr,
    //GameCard_SRAMChip_FRAM9BitAddr,
    //GameCard_SRAMChip_FRAM16BitAddr,
    //GameCard_SRAMChip_FRAM24BitAddr,

    GameCard_SRAMChip_MAX [[maybe_unused]],
} GameCard_SRAMChip;

constexpr u32 DefaultChipID  = 0x04030201;
constexpr u32 DefaultFlashID = 0x030201;

typedef struct
{
    GameCard_ROMBus ROMBusType;
    u8 ROMChipSize;
    u8 ROMPaddingByte;
    u32 ROMChipID;
    char* ROMPath;

    GameCard_SPIBus SPIBusType;
    GameCard_SRAMChip SRAMChipType;
    u8 SRAMChipSize;
    u32 FlashChipID;

    bool ImportKey1FromNTRBios7;
    char* ManualKey1Path;
} GameCardConfig;

typedef struct
{
    u8 Mode;
    bool Buffered;
    void* (*CmdHandler) (Console*, bool);
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
} GameCard;


bool GameCard_Init(GameCard* card, const char* romname, u8* bios7);
void GameCard_Cleanup(GameCard* card);
u32 GameCard_ROMDataRead(Console* sys, timestamp cur, const bool a9);
u32 GameCard_IOReadHandler(Console* sys, u32 addr, const bool a9);
void GameCard_IOWriteHandler(Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9);
void GameCard_SPIFinish(Console* sys, const bool a9);
void GameCard_HandleSchedulingROM(Console* sys, timestamp now);
