#include <stdckdint.h>
#include "pmic.h"
#include "core/console.h"
#include "core/scheduler.h"



void PMIC_Init(Console* sys)
{
    sys->PMIC.BacklightLevels.AlwaysSet = true;
}

u8 PMIC_CMDSend(Console* sys, const u8 val, const bool chipsel)
{
    PMIC* pmic = &sys->PMIC;
    u8 ret;
    if (!pmic->PrevChipSelect)
    {
        pmic->CurCmd = val;
        if (sys->SysCfg.NTRPMIC == NTRPMIC_NTR) pmic->CurCmd &= 0x83;
        if (sys->SysCfg.NTRPMIC == NTRPMIC_USG) pmic->CurCmd &= 0x87;
        pmic->CmdLen = 0;
        ret = 0;
    }
    else
    {
        if (pmic->CmdLen == 1)
        {
            switch(pmic->CurCmd)
            {
                case 0x00:
                    pmic->PowerCR.Raw = val & ((sys->SysCfg.NTRPMIC == NTRPMIC_NTR) ? 0x7F : 0x7D);
                    if (pmic->PowerCR.SystemShutDown) // TODO: this is going to need a lot of work to make accurate isn't it
                    {
                        Sched_AddEvent(sys, Sched_GetTime(&sys->Sched, Evt_SPI), Evt_HaltCore);
                    }
                    ret = 0;
                    break;
                case 0x80: ret = pmic->PowerCR.Raw; break;

                case 0x01: ret = 0; break; // read only
                case 0x81: ret = pmic->LEDRed; break;

                case 0x02:
                    pmic->MicAmpEn = val & 0x1;
                    ret = 0;
                    break;
                case 0x82: ret = pmic->MicAmpEn; break;

                case 0x03:
                    pmic->MicAmpGain = val & 0x3;
                    ret = 0;
                    break;
                case 0x83: ret = pmic->MicAmpGain; break;

                case 0x05 ... 0x07:
                    if (sys->SysCfg.NTRPMIC != NTRPMIC_USG)
                    {
                        ret = 0;
                        break;
                    }
                    [[fallthrough]];
                case 0x04:
                    MaskedWrite(pmic->BacklightLevels.Raw, val, 0x07);
                    ret = 0;
                    break;

                case 0x85 ... 0x87:
                    if (sys->SysCfg.NTRPMIC != NTRPMIC_USG)
                    {
                        ret = 0;
                        break;
                    }
                    [[fallthrough]];
                case 0x84:
                    ret = pmic->BacklightLevels.Raw;
                    if (pmic->BacklightLevels.Charging && pmic->BacklightLevels.MaxBrightWhenCharging)
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
    if (!ckd_add(&cmdtmp, pmic->CmdLen, 1))
    {
        pmic->CmdLen = cmdtmp;
    }

    pmic->PrevChipSelect = chipsel;

    return ret;
}
