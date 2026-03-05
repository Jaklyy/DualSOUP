#include <stdckdint.h>
#include "powman.h"
#include "../console.h"



u8 PowMan_CMDSend(struct Console* sys, const u8 val, const bool chipsel)
{
    Powman* pow = &sys->Powman;
    u8 ret;
    if (!pow->PrevChipSelect)
    {
        pow->CurCmd = val;
        if (sys->ConsoleModel == MODEL_NTR) pow->CurCmd &= 0x83;
        if (sys->ConsoleModel == MODEL_USG) pow->CurCmd &= 0x87;
        pow->CmdLen = 0;
        ret = 0;
    }
    else
    {
        if (pow->CmdLen == 1)
        {
            switch(pow->CurCmd)
            {
                case 0x00:
                    pow->PowerCR.Raw = val & ((sys->ConsoleModel == MODEL_NTR) ? 0x7F : 0x7D);
                    if (pow->PowerCR.SystemShutDown) // TODO: this is going to need a lot of work to make accurate isn't it
                    {
                        sys->ARM7Target = 0; // the time is now old man.
                        sys->KillThread = true;
                    }
                    ret = 0;
                    break;
                case 0x80:
                    ret = pow->PowerCR.Raw;
                    break;

                case 0x01:
                    // read only
                    ret = 0;
                    break;
                case 0x81:
                    ret = pow->LEDRed;
                    break;

                case 0x02:
                    pow->MicAmpEn = val & 0x1;
                    ret = 0;
                    break;
                case 0x82:
                    ret = pow->MicAmpEn;
                    break;

                case 0x03:
                    pow->MicAmpGain = val & 0x3;
                    ret = 0;
                    break;
                case 0x83:
                    ret = pow->MicAmpGain;
                    break;

                case 0x05 ... 0x07:
                    if (sys->ConsoleModel != MODEL_USG)
                    {
                        ret = 0;
                        break;
                    }
                    [[fallthrough]];
                case 0x04:
                    MaskedWrite(pow->BacklightLevels.Raw, val, 0x07);
                    ret = 0;
                    break;

                case 0x85 ... 0x87:
                    if (sys->ConsoleModel != MODEL_USG)
                    {
                        ret = 0;
                        break;
                    }
                    [[fallthrough]];
                case 0x84:
                    ret = pow->BacklightLevels.Raw;
                    if (pow->BacklightLevels.Charging && pow->BacklightLevels.MaxBrightWhenCharging)
                        ret |= 0x3; // force brightness to max. (checkme: does this actually set the internal reg?)
                    break;

                default: unreachable();
            }
        }
        else
        {
            ret = 0;
        }
    }

    // this is probably overkill but eh
    u16 cmdtmp;
    if (!ckd_add(&cmdtmp, pow->CmdLen, 1))
    {
        pow->CmdLen = cmdtmp;
    }

    pow->PrevChipSelect = chipsel;

    return ret;
}
