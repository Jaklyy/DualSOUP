#include <stdckdint.h>
#include "rtc.h"
#include "../console.h"




u8 RTC_ReadByte(RTC* rtc)
{
    switch (rtc->CurCmd)
    {
        case 0x86:
        {
            u32 ret = rtc->StatusReg1.Raw;
            // these clear themselves
            rtc->StatusReg1.Raw &= 0x0F;
            return ret;
        }
        case 0xC6:
        {
            return rtc->StatusReg2;
        }
        case 0xA6:
        {
            if ((rtc->BitsRead/8) >= 7) return 0; // checkme
            return rtc->DataTime[rtc->BitsRead / 8];
        }
        case 0x96:
        {
            if ((rtc->BitsRead/8) >= 3) return 0; // checkme
            return rtc->DataTime[(rtc->BitsRead / 8) + 4];
        }
        default:
            LogPrint(0, "UNIMP RTC READ %02X\n", rtc->CurCmd);
            return 0;
    }
}

void RTC_CommandHandler(RTC* rtc)
{
    const u8 val = rtc->Cmd;

    if (rtc->CmdProg == 0) rtc->CurCmd = rtc->Cmd;

    // read command
    if (rtc->CurCmd & 0x80)
    {
        return;
    }

    // checkme: does it actually care about the fixed code?
    switch(rtc->CurCmd)
    {
        case 0x06:
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

        case 0x46:
        {
            if (rtc->CmdProg == 1)
            {
                MaskedWrite(rtc->StatusReg2, val, 0xFE);
                rtc->StatusReg2 |= (val & 1); // checkme?
            }
            break;
        }
        case 0x26:
        {
            if (rtc->CmdProg <= 7)
            {
                rtc->DataTime[rtc->CmdProg-1] = val;
            }
            break;
        }
        case 0x66:
        {
            if (rtc->CmdProg <= 3)
            {
                rtc->DataTime[(rtc->CmdProg-1)+4] = val;
            }
            break;
        }
        default:
            LogPrint(0, "I HAVE TO DO THIS??? %02X\n", rtc->CurCmd);
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
            rtc->Cmd = 0;
        }
        else if (!rtc->CR.ClockHi) // clock must be driven low to send bits?
        {
            if (rtc->CR.DataDir)
            {
                // write
                rtc->Cmd |= (val & mask & 1) << rtc->BitsSent;
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
