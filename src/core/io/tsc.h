#pragma once
#include "../utils.h"




typedef struct
{
    union
    {
        u8 Raw;
        struct
        {
            u8 PowerDownMode : 2;
            bool ReferenceSelect : 1;
            bool ConversionMode : 1;
            u8 ChannelSel : 3;
            bool StartBit;
        };
    } ControlByte;
    //bool PrevChipSelect;
    u16 Ret;
    u8 CmdLen;
} TSC;

u8 TSC_SendCommand(TSC* tsc, const u8 val);
