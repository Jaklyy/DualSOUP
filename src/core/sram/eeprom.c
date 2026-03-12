#include <stdckdint.h>
#include <stdlib.h>
#include "eeprom.h"




void EEPROM_Init(EEPROM* eep, u8* ram, u64 size, u8 addrbytes, u8 writeprot)
{
    eep->RAMSize = size;
    eep->AddrBytes = addrbytes;
    //flash->WriteProt = writeprot ? ~((size/4)-1) : 0;
    eep->RAM = ram;
}

void EEPROM_Cleanup(EEPROM* eep)
{
    // TODO: flush changes
    if (eep->RAM != nullptr)
    {
        free(eep->RAM);
        eep->RAM = nullptr;
    }
}

void EEPROM_Reset(EEPROM* eep)
{
    eep->WriteEnabled = false;
    eep->CmdLen = 0;
    eep->Busy = 0;
    eep->PrevChipSelect = false;
}



u8 EEPROM_WriteEnable(EEPROM* eep, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (chipsel) return 0xFF;
    eep->WriteEnabled = true;
    return 0;
}

u8 EEPROM_WriteDisable(EEPROM* eep, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (chipsel) return 0xFF;
    eep->WriteEnabled = false;
    return 0;
}

u8 EEPROM_ReadStatus(EEPROM* eep)
{
    return 0xF0 | (eep->WriteProt << 2) | (eep->WriteEnabled << 1) | eep->Busy;
}

u8 EEPROM_WriteStatus(EEPROM* eep, const u8 val)
{
    // TODO: setup write protection.

    eep->WriteProt = (val >> 2) & 0x3;

    return 0;
}

u8 EEPROM_ReadData(EEPROM* eep, const u8 val)
{
    // checkme??
    if (eep->Busy) return 0xFF;

    if ((eep->CmdLen < (eep->AddrBytes+1)))
    {
        if (eep->CmdLen != 0)
        {
            eep->CurAddr = (eep->CurAddr << 8) | val;
        }
        return 0;
    }
    else
    {
        // checkme: how does it actually do masking?
        eep->CurAddr &= (eep->RAMSize-1);
        u32 ret = eep->RAM[eep->CurAddr++];
        return ret;
    }
}

u8 EEPROM_WriteData(EEPROM* eep, const u8 val, const bool chipsel)
{
    if (!eep->WriteEnabled || eep->Busy) return 0xFF;

    if ((eep->CmdLen < (eep->AddrBytes+1)))
    {
        if (eep->CmdLen != 0)
        {
            eep->CurAddr = (eep->CurAddr << 8) | val;
        }
        return 0;
    }
    else
    {
        // checkme: how does it actually do masking?
        eep->CurAddr &= (eep->RAMSize-1);
        eep->RAM[eep->CurAddr++] = val;
        // TODO: handle pages?
        // TODO: write protection?
    }
    if (!chipsel) eep->WriteEnabled = false;
    return 0;
}

u8 EEPROM_CMDSend(EEPROM* eep, const u8 val, const bool chipsel)
{
    if (!eep->PrevChipSelect)
    {
        eep->CurCmd = val;
        eep->CmdLen = 0;
        eep->CurAddr = 0;
    }

    // checkme: how does continuing cmd submission after one fails work?
    u8 ret;
    switch(eep->CurCmd)
    {
        case 0x06: ret = EEPROM_WriteEnable(eep, chipsel); break;
        case 0x04: ret = EEPROM_WriteDisable(eep, chipsel); break;
        case 0x05: ret = EEPROM_ReadStatus(eep); break;
        case 0x01: ret = EEPROM_WriteStatus(eep, val); break;
        case 0x0B:
            if (eep->AddrBytes != 1)
            {
                ret = 0xFF; /* checkme */
                break;
            }
            else
            {
                eep->CurAddr = 0x01;
                [[fallthrough]];
            }
        case 0x03: ret = EEPROM_ReadData(eep, val); break;
        case 0x0A:
            if (eep->AddrBytes != 1)
            {
                ret = 0xFF; /* checkme */
                break;
            }
            else
            {
                eep->CurAddr = 0x01;
                [[fallthrough]];
            }
        case 0x02: ret = EEPROM_WriteData(eep, val, chipsel); break;

        default: ret = 0xFF; LogPrint(LOG_FLASH | LOG_ODD, "UNKNOWN EEPROM COMMAND %02X\n", eep->CurCmd); break;
    }
    // this is probably overkill but eh
    u16 cmdtmp;
    if (!ckd_add(&cmdtmp, eep->CmdLen, 1))
    {
        eep->CmdLen = cmdtmp;
    }

    eep->PrevChipSelect = chipsel;

    return ret;
}