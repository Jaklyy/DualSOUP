#pragma once
#include "../utils.h"




typedef struct
{
    union
    {
        u8 Raw;
        struct
        {
            bool SoundAmpEn : 1;
            bool SoundAmpMute : 1;
            bool BotBacklight : 1;
            bool TopBacklight : 1;
            bool LEDBlinkEn : 1;
            bool LEDBlinkSpeed : 1; // set = faster
            bool SystemShutDown : 1;
        };
    } PowerCR;
    bool LEDRed;
    bool MicAmpEn;
    u8 MicAmpGain : 2;
    union
    {
        u8 Raw;
        struct
        {
            u8 BacklightBrightness : 2;
            bool MaxBrightWhenCharging : 1;
            bool Charging : 1;
            u8 : 2;
            bool AlwaysSet;
        };
    } BacklightLevels;
    bool PrevChipSelect;
    u8 CurCmd;
    u8 CmdLen;
} Powman;

u8 PowMan_CMDSend(Powman* pow, const u8 val, const bool chipsel);
