#pragma once
#include "core/utils.h"




typedef struct
{
    u64 DirtyCycle;
    u16 CmdLen;
    bool Dirty;
    bool Busy;
    bool WriteEnabled;
    bool PrevChipSelect;
    bool PowerDown;
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
void Flash_Init(Flash* flash, u8* ram, u64 size, bool writeprot, const int id);
void Flash_Cleanup(Flash* flash);
void Flash_Reset(Flash* flash);
bool Flash_FlushToDisk(Flash* flash, u8** outbuf, u64* size, bool force);
