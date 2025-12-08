#pragma once
#include "../utils.h"




typedef struct
{
    u16 CmdLen;
    bool WriteEnabled;
    bool Busy;
    bool PowerDown;
    bool PrevChipSelect;
    u8 CurCmd;
    u8 WritePos;
    u32 CurAddr;
    u8 DataBuffer[256];
    // configurable
    u32 RAMSize;
    u32 WriteProt;
    u8 ID[3];
    u8* RAM;
} Flash;

u8 Flash_CMDSend(Flash* flash, const u8 val, const bool chipsel);
bool Flash_Init(Flash* flash, FILE* ram, u64 size, bool writeprot, const int id, const char* str);
void Flash_Cleanup(Flash* flash);
void Flash_Reset(Flash* flash);
