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

constexpr timestamp GamePak_AGBWaitStates[4][4] =
{
    {4, 3, 2, 8}, // nseq
    {2, 1}, // seq 0
    {4, 1}, // seq 1
    {8, 1}, // seq 2
};

constexpr timestamp GamePak_NTRWaitStates[2][4] =
{
    {9, 7, 5, 17}, // nseq
    {5, 3}, // seq
};

typedef struct
{
    // state
    union {
        u32 LatchedAddr_Full;
        struct {
            u16 LatchedAddr_Lo;
            u8 LatchedAddr_Hi;
        };
    };
    bool Write_Sel;
    bool Read_Sel;
    bool ROM_CSel;
    bool RAM_CSel;
    bool IRQ_Out;

    // rom
    bool Inserted;
    struct {
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

typedef struct
{
    //bool Power_Out; // pin 0
    //bool Clock_Out; // pin 1
    bool Write_Sel; // pin 2
    bool Read_Sel; // pin 3
    bool ROM_CSel; // pin 4
    union {
        u32 Addr_Full;
        struct {
            union {
                u16 Addr_Lo;
                u16 ROM_Data;
            }; // pins 5 ... 22
            union {
                u8 Addr_Hi;
                u8 RAM_Data;
            }; // pins 23 ... 28
        };
    };
    bool RAM_CSel; // pin 29
    bool IRQ_In; // pin 30
    //bool Ground; // pin 31

    bool ROMBurst;
    bool RAMBurst;
} BusGamePak;


typedef struct Console Console;

// handlers for cart hardware
void GamePak_Init(GamePak* pak);
void GamePak_Step(BusGamePak* bus, GamePak* pak);
void GamePakBus_ROMHandler(Console* sys, timestamp now, bool a9);
void GamePakBus_RAMHandler(Console* sys, timestamp now, bool a9);
