#pragma once
#include "../utils.h"




typedef struct
{
    u16 CmdLen;
    bool Busy;
    bool WriteEnabled;
    bool PrevChipSelect;
    u8 CurCmd;
    u8 AddrBytes;
    u8 WriteProt;
    u32 CurAddr;
    // configurable
    u32 RAMSize;
    //u32 WriteProtMask; TODO
    //u8 ID[3]; checkme?
    u8* RAM;
} EEPROM;

u8 EEPROM_CMDSend(EEPROM* eep, const u8 val, const bool chipsel);
bool EEPROM_Init(EEPROM* flash, FILE* ram, u64 size, u8 addrbytes, u8 writeprot);
void EEPROM_Cleanup(EEPROM* eep);
void EEPROM_Reset(EEPROM* eep);
