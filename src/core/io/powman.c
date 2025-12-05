#include <stdckdint.h>
#include "powman.h"
#include "../console.h"




u8 PowMan_CMDSend(Powman* pow, const u8 val, const bool chipsel)
{

    u8 ret;
    if (!pow->PrevChipSelect)
    {
        pow->CurCmd = val & 0x87; // TODO: DS Fat?
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
                    pow->PowerCR.Raw = val & 0x7F;
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
                    pow->MicAmpEn = val & 1;
                    ret = 0;
                    break;
                case 0x82:
                    ret = pow->MicAmpEn;
                    break;

                case 0x03:
                    pow->MicAmpGain = val & 2;
                    ret = 0;
                    break;
                case 0x83:
                    ret = pow->MicAmpGain;
                    break;

                case 0x04 ... 0x07:
                    MaskedWrite(pow->BacklightLevels.Raw, val, 0x0F);
                    ret = 0;
                    break;

                case 0x84 ... 0x87:
                    ret = pow->BacklightLevels.Raw;
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
