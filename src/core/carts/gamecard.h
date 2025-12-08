#pragma once
#include "../utils.h"



struct Console;

enum EncryptMode : u8
{
    Unenc,
    Key1,
    Key2,
};


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
    u32 Key1[0x412];
} Gamecard;


bool Gamecard_Init(Gamecard* card, const char* romname, u8* bios7);
void Gamecard_Cleanup(Gamecard* card);
u32 Gamecard_ROMDataRead(struct Console* sys, timestamp cur, const bool a9);
u32 Gamecard_IOReadHandler(struct Console* sys, u32 addr, timestamp cur, const bool a9);
void Gamecard_IOWriteHandler(struct Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9);
