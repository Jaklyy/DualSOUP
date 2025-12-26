#include <SDL3/SDL_time.h>
#include <stdckdint.h>
#include "rtc.h"
#include "../console.h"




void RTC_Init(RTC* rtc)
{
    rtc->StatusReg1.Clock24 = true;
}

u8 RTC_FromBCD(u8 bcd)
{
    // make sure the bcd value is valid
    if ((bcd & 0xF) >= 10)
    {
        // return 255 since that'll ensure it gets error handled properly.
        return 255;
    }
    return (bcd & 0xF) + ((bcd>>4) * 10);
}

u8 RTC_ToBCD(u8 num)
{
    return (num & 0xF) + ((num/10) << 4);
}

void RTC_UpdateFull(RTC* rtc)
{
    u8 year = RTC_FromBCD(rtc->DataTime[0]);
    if (year > 99) year = 0;

    u8 month = RTC_FromBCD(rtc->DataTime[1]) - 1;
    if (month > (12-1)) month = 0;

    u8 day = RTC_FromBCD(rtc->DataTime[2]) - 1;
    if (day > (31-1)) day = 0;

    //u8 
}

u8 RTC_ReadByte(RTC* rtc)
{
    //printf("read: %02X\n", rtc->CurCmd);
    switch (rtc->CurCmd)
    {
        case 0x61: // status reg 1
        {
            u32 ret = rtc->StatusReg1.Raw;
            // these clear themselves
            rtc->StatusReg1.Raw &= 0x0F;
            return ret;
        }
        case 0x63: // status reg 2
        {
            return rtc->StatusReg2;
        }
        case 0x65: // rtc y/m/d/dow/h/m/s
        {
            SDL_Time time;
            SDL_GetCurrentTime(&time);
            SDL_DateTime date;
            SDL_TimeToDateTime(time, &date, true);
            switch(rtc->BitsRead/8)
            {
            case 0: return RTC_ToBCD(date.year % 100);
            case 1: return RTC_ToBCD(date.month);
            case 2: return RTC_ToBCD(date.day);
            case 3: return RTC_ToBCD(date.day_of_week);
            case 4: return RTC_ToBCD(rtc->StatusReg1.Clock24 ? date.hour : (date.hour%12)) | ((date.hour >= 12) << 6);
            case 5: return RTC_ToBCD(date.minute);
            case 6: return RTC_ToBCD(date.second);
            default: return 0;
            }
            //if ((rtc->BitsRead/8) >= 7) return 0; // checkme
            //return rtc->DataTime[rtc->BitsRead / 8];
        }
        case 0x67: // rtc h/m/s
        {
            SDL_Time time;
            SDL_GetCurrentTime(&time);
            SDL_DateTime date;
            SDL_TimeToDateTime(time, &date, true);
            switch(rtc->BitsRead/8)
            {
            case 0: return RTC_ToBCD(rtc->StatusReg1.Clock24 ? date.hour : (date.hour%12)) | ((date.hour >= 12) << 6);
            case 1: return RTC_ToBCD(date.minute);
            case 2: return RTC_ToBCD(date.second);
            default: return 0;
            }
            //if ((rtc->BitsRead/8) >= 3) return 0; // checkme
            //return rtc->DataTime[(rtc->BitsRead / 8) + 4];
        }
        case 0x69:
        {
            if (rtc->CmdProg <= 3)
            {
                return rtc->IRQ1[rtc->CmdProg-1];
            }
            break;
        }
        case 0x6B:
        {
            if (rtc->CmdProg <= 3)
            {
                return rtc->IRQ2[rtc->CmdProg-1];
            }
            break;
        }
        case 0x6D:
        {
            if (rtc->CmdProg == 1)
                return rtc->ClockAdjust;
            break;
        }
        case 0x6F:
        {
            if (rtc->CmdProg == 1)
                return rtc->ClockAdjust;
            break;
        }
        default:
            LogPrint(LOG_UNIMP | LOG_RTC, "UNIMP RTC READ %02X\n", rtc->CurCmd);
            return 0;
    }
}

void RTC_CommandHandler(RTC* rtc)
{
    const u8 val = rtc->Cmd;

    if (rtc->CmdProg == 0)
    {
        rtc->CurCmd = rtc->Cmd;
        return;
    }
    //printf("%08X\n", rtc->CurCmd);

    // read command
    if (rtc->CurCmd & 0x01)
    {
        return;
    }
    printf("writes %02X %02X\n", rtc->CurCmd, rtc->Cmd);

    // checkme: does it actually care about the fixed code?
    switch(rtc->CurCmd)
    {
        case 0x60: // status reg 1
        {
            if (rtc->CmdProg == 1)
            {
                if (val & 1)
                {
                    // reset rtc
                }

                MaskedWrite(rtc->StatusReg1.Raw, val, 0x0E);
            }
            break;
        }

        case 0x62: // status reg 2
        {
            if (rtc->CmdProg == 1)
            {
                MaskedWrite(rtc->StatusReg2, val, 0xFE);
                rtc->StatusReg2 |= (val & 1); // checkme?
            }
            break;
        }
        case 0x64: // rtc y/m/d/dow/h/m/s
        {
            if (rtc->CmdProg <= 7)
            {
                rtc->DataTime[rtc->CmdProg-1] = val;
            }
            if (rtc->CmdProg == 7)
            {

            }
            break;
        }
        case 0x66: // rtc h/m/s
        {
            if (rtc->CmdProg <= 3)
            {
                rtc->DataTime[(rtc->CmdProg-1)+4] = val;
            }
            break;
        }
        case 0x68:
        {
            if (rtc->CmdProg <= 3)
            {
                rtc->IRQ1[rtc->CmdProg-1] = val;
            }
            break;
        }
        case 0x6A:
        {
            if (rtc->CmdProg <= 3)
            {
                rtc->IRQ2[rtc->CmdProg-1] = val;
            }
            break;
        }
        case 0x6C:
        {
            if (rtc->CmdProg == 1)
                rtc->ClockAdjust = val;
            break;
        }
        case 0x6E:
        {
            if (rtc->CmdProg == 1)
                rtc->ClockAdjust = val;
            break;
        }
        default:
            LogPrint(LOG_UNIMP | LOG_RTC, "I HAVE TO DO THIS??? %02X\n", rtc->CurCmd);
            break;
    }
}

void RTC_IOWriteHandler(struct Console* sys, const u16 val, const u16 mask)
{
    // checkme: how do byte writes work actually?
    RTC* rtc = &sys->RTC;
    bool oldchipsel = rtc->CR.ChipSel;

    MaskedWrite(rtc->CR.Raw, val, mask & 0xFFFE);
    if (rtc->CR.DataDir && mask &1) rtc->CR.DataIO = val & 1;
    if (rtc->CR.ChipSel)
    {
        if (oldchipsel == false) // first bit after chip sel raised is ignored?
        {
            rtc->BitsSent = 0;
            rtc->CmdProg = 0;
            rtc->Cmd = 0;
        }
        else if (!rtc->CR.ClockHi) // clock must be driven low to send bits?
        {
            if (rtc->CR.DataDir)
            {
                // write
                rtc->Cmd = (rtc->Cmd << 1) | (val & mask & 1);
                rtc->BitsSent++;

                if (rtc->BitsSent >= 8)
                {
                    rtc->BitsSent = 0;
                    RTC_CommandHandler(rtc);
                    rtc->CmdProg++;
                    rtc->Cmd = 0;
                }
            }
            else
            {
                // read
                rtc->CR.DataIO = (RTC_ReadByte(rtc) >> (rtc->BitsRead % 8)) & 1;

                if (ckd_add(&rtc->BitsRead, rtc->BitsRead, 1))
                    rtc->BitsRead = 255;
            }
        }
    }
}
