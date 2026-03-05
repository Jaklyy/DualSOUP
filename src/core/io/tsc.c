#include <stdckdint.h>
#include <SDL3/SDL_mouse.h>
#include "tsc.h"




u8 TSC_SendCommand(TSC* tsc, const u8 val)
{
    if (val & 0x80)
    {
        tsc->ControlByte.Raw = val;
        //tsc->CmdLen = 0;

        float a;
        switch(tsc->ControlByte.ChannelSel)
        {
        case 0: // temp 0
            //LogPrint(LOG_UNIMP|LOG_TSC, "UNIMPLEMENTED: TSC TEMP REG 0. %02X\n", tsc->ControlByte.Raw);
            tsc->Ret = 0; break;

        case 1: // touch y
            if (SDL_BUTTON_LMASK & SDL_GetMouseState(NULL, &a))
            {
                if (a >= (192*2))
                {
                    tsc->Ret = (u16)((a - (192*2)) * (256 * 16) / (192*2));
                }
                else tsc->Ret = 0xFFF;
            }
            else tsc->Ret = 0xFFF;
            break;

        case 2: // battery voltage (grounded)
            LogPrint(LOG_ODD|LOG_TSC, "READING GROUNDED TSC BATTERY REG? %02X\n", tsc->ControlByte.Raw);
            tsc->Ret = 0; break;

        case 3: // touch z1
            LogPrint(LOG_UNIMP|LOG_TSC, "UNIMPLEMENTED: TSC Z1 PRESSURE REG. %02X\n", tsc->ControlByte.Raw);
            tsc->Ret = 0xFFF; break;

        case 4: // touch z2
            LogPrint(LOG_UNIMP|LOG_TSC, "UNIMPLEMENTED: TSC Z2 PRESSURE REG. %02X\n", tsc->ControlByte.Raw);
            tsc->Ret = 0xFFF; break;

        case 5: // touch xRet
            if (SDL_BUTTON_LMASK & SDL_GetMouseState(&a, NULL))
            {
                tsc->Ret = (u16)(a * 16 / 2);
            }
            else tsc->Ret = 0xFFF;
            break;

        case 6: // AUX (mic)
            LogPrint(LOG_UNIMP|LOG_TSC, "UNIMPLEMENTED: MIC. %02X\n", tsc->ControlByte.Raw);
            tsc->Ret = 0; break;

        case 7: // temp 1
            LogPrint(LOG_UNIMP|LOG_TSC, "UNIMPLEMENTED: TSC TEMP REG 1. %02X\n", tsc->ControlByte.Raw);
            tsc->Ret = 0; break;
        }

        tsc->Ret <<= 3;

        return 0; // idk
    }
    else
    {
        u8 ret = tsc->Ret >> 8;
        tsc->Ret <<= 8;
        return ret;
    }

    // this is probably overkill but eh
    /*u16 cmdtmp;
    if (!ckd_add(&cmdtmp, tsc->CmdLen, 1))
    {
        tsc->CmdLen = cmdtmp;
    }*/
}
