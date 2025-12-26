#pragma once
#include "../utils.h"




typedef struct
{
    union
    {
        u16 Raw;
        struct
        {
            bool DataIO : 1;
            bool ClockHi : 1;
            bool ChipSel : 1;
            bool : 1;
            bool DataDir : 1;
            bool ClockDir : 1;
            bool SelDir : 1;
        };
    } CR;

    union
    {
        u8 Raw;
        struct
        {
            bool PowerOn : 1;
            bool SomethingVoltage : 1;
            bool Int2 : 1;
            bool Int1 : 1;
            bool SRAMControl1 : 1;
            bool SRAMControl0 : 1;
            bool Clock24 : 1;
            bool Reset : 1;
        };
    } StatusReg1;

    u8 StatusReg2; // TODO

    u8 DataTime[7];

    u8 BitsSent;
    u8 BitsRead;
    u8 Cmd;
    u8 CurCmd;
    u8 CmdProg;
    u8 DataOut;
    u8 DayOfWeek;
    u8 IRQ1[3];
    u8 IRQ2[3];
    u8 ClockAdjust;
    u8 FreeReg;
    u64 SecondsSince0;
} RTC;

struct Console;
void RTC_IOWriteHandler(struct Console* sys, const u16 val, const u16 mask);
void RTC_Init(RTC* rtc);
