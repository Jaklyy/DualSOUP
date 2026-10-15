#pragma once
#include "core/utils.h"
#include "core/sram/cardsram.h"
#include "core/sram/ir.h"



typedef struct Console Console;

enum EncryptMode : u8
{
    Unenc,
    Key1,
    Key2,
};

typedef enum : u8
{
    GameCard_ROMBus_Ejected,
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

constexpr u32 DefaultChipID  = 0x00000201;
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
    char* FolderPath;

    bool ImportKey1FromNTRBios7;
    char* ManualKey1Path;

    char* FriendlyName;
} GameCardCfg;

typedef union
{
    GCSRAM SRAM;
    IRhle IRhle;
} GCSPI;


typedef struct GameCard GameCard;
struct GameCard
{
    GameCard_ROMBus ROMBusType;
    u32 RomSize;
    u32 ChipID;
    u32* ROM;
    char* SavFolder;
    GameCard_SPIBus SPIType;
    GameCard_SRAMChip SRAMType;
    GCSPI SPI;
    u32 Key1[4168/sizeof(u32)];

    u32 (*ReadHandler) (struct GameCard*);
    u32 Address;
    s32 NumWords;
    u32 WordBuffer;
    u8 Mode;
    bool Buffered;
};


bool GameCard_Init(GameCard* card, const GameCardCfg* cfg, u8* bios7);
bool GameCardSPI_FlushToDisk(GameCard* card, u8** outbuf, u64* size, bool force);
void GameCard_Cleanup(GameCard* card);
u32 GameCard_ROMDataRead(Console* sys, timestamp cur, const bool a9);
u32 GameCard_IOReadHandler(Console* sys, u32 addr, const bool a9);
void GameCard_IOWriteHandler(Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9);
void GameCard_SPIFinish(Console* sys, const bool a9);
void GameCard_HandleSchedulingROM(Console* sys, timestamp now);
