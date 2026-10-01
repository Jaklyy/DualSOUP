#pragma once
#include "core/utils.h"
#include "cardsram.h"



typedef struct
{
    u16 CmdLen;
    bool PrevChipSelect;
    u8 CurCmd;
    GCSRAM SRAM;
} IRhle;

u8 IRhle_CMDSend(IRhle* ir, GameCard_SRAMChip sramtype, const u8 val, const bool chipsel);
void IRhle_Cleanup(IRhle* ir, GameCard_SRAMChip sramtype);
