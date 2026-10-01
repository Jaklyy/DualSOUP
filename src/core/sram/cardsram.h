#pragma once
#include "flash.h"
#include "eeprom.h"

typedef union
{
    Flash Flash;
    EEPROM EEP;
} GCSRAM;

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

u8 GameCardSRAM_CmdSend(GCSRAM* self, GameCard_SRAMChip sram, u8 val, bool chipsel);
void GameCardSRAM_Cleanup(GCSRAM* self, GameCard_SRAMChip sram);
