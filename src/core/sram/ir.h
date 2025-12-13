#include "../utils.h"
#include "flash.h"



typedef struct
{
    u16 CmdLen;
    bool PrevChipSelect;
    u8 CurCmd;
    Flash Flash;
} IRhle;

u8 IRhle_CMDSend(IRhle* ir, const u8 val, const bool chipsel);
