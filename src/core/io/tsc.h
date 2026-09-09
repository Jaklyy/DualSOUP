#pragma once
#include "core/utils.h"




typedef struct
{
    u16 X;
    u16 Y;
    u16 Z1;
    u16 Z2;
    bool Touched;
} TouchState;

typedef struct
{
    TouchState State;
    union
    {
        u8 Raw;
        struct
        {
            u8 PowerDownMode : 2;
            bool ReferenceSelect : 1;
            bool ConversionMode : 1;
            u8 ChannelSel : 3;
            bool StartBit : 1;
        };
    } ControlByte;
    u8 CmdLen;
    //bool PrevChipSelect;
    u16 Ret;
} TSC;

u8 TSC_SendCommand(TSC* tsc, const u8 val);
