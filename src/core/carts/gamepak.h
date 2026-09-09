#pragma once
#include "core/utils.h"




typedef enum : u8
{
    GamePakSRAM_None,
    GamePakSRAM_Flash,
    GamePakSRAM_SRAM,
} GamePakSRAMType;

typedef enum : u8
{
    GamePakFlash_Atmel,
    GamePakFlash_SST,
    GamePakFlash_Panasonic,
    GamePakFlash_Macronix,
    GamePakFlash_Sanyo,
} GamePakFlashType;

typedef enum : u8
{
    GPFCmd_EraseAll = 0x10,
    GPFCmd_EraseSector = 0x30,
    GPFCmd_ErasePrefix = 0x80,
    GPFCmd_ChipIDEnable = 0x90,
    GPFCmd_Write = 0xA0,
    GPFCmd_SwitchBank = 0xB0,
    GPFCmd_TerminateCmd = 0xF0,
} GamePakFlashCmds;

typedef enum : u8
{
    GPFCmdProg_Begin,
    GPFCmdProg_Begin2,
    GPFCmdProg_CmdEntry,
    GPFCmdProg_EraseCmdEntry,
    GPFCmdProg_BankEntry,
    GPFCmdProg_WriteDataBase,
} GamePakFlashCmdProg;

typedef struct
{
    // rom
    struct
    {
        u8 IODir;
        bool ReadEnable;
        bool Present;
    } GPIO;
    u32 EEPROMBegin;
    size_t ROMSize;
    size_t EEPROMSize;
    u64* EEPROM;
    u16* ROM;
    // sram

    // flash
    u8 FlashChipID[2];
    u8 FlashCmdProg;
    bool FlashChipIDMode;
    bool FlashErasePrefixed;
    GamePakFlashType FlashType;

    // generic
    GamePakSRAMType SRAMType;
    size_t SRAMSize;
    size_t SRAMBankOffs;
    u8* SRAM;
} GamePak;


// handlers for cart hardware
void GamePak_Init(GamePak* pak);

u16 GamePak_ROMRead(GamePak* pak, u32 addr);
void GamePak_ROMWrite(GamePak* pak, u32 addr, u16 val);

u8 GamePak_SRAMRead(GamePak* pak, const u16 addr);
void GamePak_SRAMWrite(GamePak* pak, const u16 addr, const u8 val);
