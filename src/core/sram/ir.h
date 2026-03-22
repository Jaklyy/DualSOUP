#include "../utils.h"
#include "flash.h"



typedef struct
{
    u16 CmdLen;
    bool PrevChipSelect;
    u8 CurCmd;
    void* SRAM;
    u8 (*SRAM_CMDSend)(void*, const u8, const bool);
    u8 (*SRAM_Cleanup)(void*);
} IRhle;

u8 IRhle_CMDSend(IRhle* ir, const u8 val, const bool chipsel);
void IRhle_Cleanup(IRhle* ir);
