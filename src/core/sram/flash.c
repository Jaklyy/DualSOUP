#include <stdckdint.h>
#include <stdbit.h>
#include <stdlib.h>
#include "flash.h"




void Flash_Init(Flash* flash, u8* ram, u64 size, bool writeprot, const int id)
{
    flash->RAMSize = size;
    flash->WriteProt = writeprot ? ~((size/4)-1) : (u32)~0;
    flash->RAM = ram;

    flash->ID[0] = (id>> 0) & 0xFF;
    flash->ID[1] = (id>> 8) & 0xFF;
    flash->ID[2] = (id>>16) & 0xFF;
}

void Flash_Cleanup(Flash* flash)
{
    // TODO: flush changes
    if (flash->RAM != nullptr)
    {
        free(flash->RAM);
        flash->RAM = nullptr;
    }
}

void Flash_Reset(Flash* flash)
{
    flash->WriteEnabled = false;
    flash->PowerDown = false;
    flash->CmdLen = 0;
    flash->Busy = 0;
    flash->PrevChipSelect = false;
}


u8 Flash_WriteEnable(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (flash->PowerDown || chipsel)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Write Enable?\n");
        return 0xFF;
    }
    flash->WriteEnabled = true;
    return 0;
}

u8 Flash_WriteDisable(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (flash->PowerDown || chipsel)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Write Disable?\n");
        return 0xFF;
    }
    flash->WriteEnabled = false;
    return 0;
}

u8 Flash_EnterDeepPowerDown(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (chipsel || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Enter Power Down?\n");
        return 0xFF;
    }
    flash->PowerDown = true;
    return 0;
}

u8 Flash_ExitDeepPowerDown(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (chipsel || flash->Busy) 
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Exit Power Down?\n");
        return 0xFF;
    }
    flash->PowerDown = false;
    return 0;
}

u8 Flash_ReadID(Flash* flash)
{
    if (flash->PowerDown || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Read ID?\n");
        return 0xFF;
    }

    if (flash->CmdLen < 3)
    {
        return flash->ID[flash->CmdLen];
    }
    else return 0; // checkme?
}

u8 Flash_ReadStatus(Flash* flash)
{
    if (flash->PowerDown)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Read Status?\n");
        return 0xFF;
    }

    return (flash->WriteEnabled << 1) | flash->Busy;
}

u8 Flash_ReadData(Flash* flash, const u8 val)
{
    if (flash->PowerDown || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Read Data?\n");
        return 0xFF;
    }

    if (flash->CmdLen < 4)
    {
        if (flash->CmdLen != 0)
        {
            flash->CurAddr = (flash->CurAddr << 8) | val;
        }
    }
    else
    {
        // checkme: how does it actually do masking?
        u8 ret = flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)];
        return ret;
    }
    return 0;
}

u8 Flash_ReadDataFast(Flash* flash, const u8 val)
{
    if (flash->PowerDown || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Read Data Fast?\n");
        return 0xFF;
    }

    if (flash->CmdLen < 5)
    {
        if (flash->CmdLen != 0 && flash->CmdLen != 4)
        {
            flash->CurAddr = (flash->CurAddr << 8) | val;
        }
        return 0;
    }
    else
    {
        // TODO: >256 bytes being written causes problems???
        // checkme: how does it actually do masking?
        return flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)];
    }
}


u8 Flash_PageWrite(Flash* flash, const u8 val, const bool chipsel)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Page Write?\n");
        return 0xFF;
    }

    if ((flash->CmdLen < 4))
    {
        if (flash->CmdLen != 0)
        {
            flash->CurAddr = (flash->CurAddr << 8) | val;
        }
        return 0;
    }
    else if (flash->CurAddr & flash->WriteProt)
    {
        if ((flash->CmdLen - 3) >= 256)
        {
            flash->CmdLen = 256 + 3;
        }

        // checkme: how does it actually do masking?
        flash->DataBuffer[flash->WritePos] = val;
        flash->WritePos++;

        flash->WritePos &= 0xFF;

        if (!chipsel)
        {
            // CHECKME: is this correct behavior for buffer overflow?
            for (int i = 0; i < flash->CmdLen - 3; i++)
            {
                flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)] = flash->DataBuffer[i];

                // wrap value to page boundary
                if (!(flash->CurAddr & 0xFF))
                    flash->CurAddr -= 256;
            }
        }
    }
    if (!chipsel) flash->WriteEnabled = false;
    return 0;
}

u8 Flash_PageProgram(Flash* flash, const u8 val, const bool chipsel)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Page Program?\n");
        return 0xFF;
    }

    if ((flash->CmdLen < 4))
    {
        if (flash->CmdLen != 0)
        {
            flash->CurAddr = (flash->CurAddr << 8) | val;
        }
        return 0;
    }
    else if (flash->CurAddr & flash->WriteProt)
    {
        if (chipsel)
        {
            if ((flash->CmdLen - 3) >= 256)
            {
                flash->CmdLen = 256 + 3;
            }

            // checkme: how does it actually do masking?
            flash->DataBuffer[flash->WritePos++] = val;

            flash->WritePos &= 0xFF;
        }
        else
        {
            // CHECKME: is this correct behavior for buffer overflow?
            for (int i = 0; i < flash->CmdLen - 3; i++)
            {
                // checkme: what does this actually do...?
                flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)] &= ~flash->DataBuffer[i];

                // wrap value to page boundary
                if (!(flash->CurAddr & 0xFF))
                    flash->CurAddr -= 256;
            }
        }
    }
    if (!chipsel) flash->WriteEnabled = false;
    return 0;
}

u8 Flash_PageErase(Flash* flash, const u8 val, const bool chipsel)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Page Erase?\n");
        return 0xFF;
    }

    // TODO chipsel?
    if ((flash->CmdLen < 4))
    {
        // just dont set the low byte tbh
        if (flash->CmdLen > 1)
        {
            flash->CurAddr = (flash->CurAddr << 8) | val;
        }
        return 0;
    }
    else if (flash->CurAddr & flash->WriteProt)
    {
        for (int i = 0; i < 0x100; i++)
        {
            // apparently erase means set...?
            flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)] = 0xFF;
        }
    }
    if (!chipsel) flash->WriteEnabled = false;
    return 0;
}

u8 Flash_SectorErase(Flash* flash, const u8 val, const bool chipsel)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy)
    {
        LogPrint(LOG_FLASH|LOG_ODD, "Flash: Busy Sector Erase?\n");
        return 0xFF;
    }

    // TODO chipsel?
    if ((flash->CmdLen < 4))
    {
        // just dont set the low bytes tbh
        if (flash->CmdLen > 2)
        {
            flash->CurAddr = (flash->CurAddr << 8) | val;
        }
        return 0;
    }
    else if (flash->CurAddr & flash->WriteProt)
    {
        for (int i = 0; i < 0x10000; i++)
        {
            // apparently erase means set...?
            flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)] = 0xFF;
        }
    }
    if (!chipsel) flash->WriteEnabled = false;
    return 0;
}


u8 Flash_CMDSend(Flash* flash, const u8 val, const bool chipsel)
{
    if (!flash->PrevChipSelect)
    {
        flash->CurCmd = val;
        flash->CmdLen = 0;
        flash->CurAddr = 0;
    }

    // checkme: how does continuing cmd submission after one fails work?
    u8 ret;
    switch(flash->CurCmd)
    {
        case 0x06: ret = Flash_WriteEnable(flash, chipsel); break;
        case 0x04: ret = Flash_WriteDisable(flash, chipsel); break;
        case 0x9F: ret = Flash_ReadID(flash); break;
        case 0x05: ret = Flash_ReadStatus(flash); break;
        case 0x03: ret = Flash_ReadData(flash, val); break;
        case 0x0B: ret = Flash_ReadDataFast(flash, val); break;
        case 0x0A: ret = Flash_PageWrite(flash, val, chipsel); break;
        case 0x02: ret = Flash_PageProgram(flash, val, chipsel); break;
        case 0xDB: ret = Flash_PageErase(flash, val, chipsel); break;
        case 0xD8: ret = Flash_SectorErase(flash, val, chipsel); break;
        case 0xB9: ret = Flash_EnterDeepPowerDown(flash, chipsel); break;
        case 0xA9: ret = Flash_ExitDeepPowerDown(flash, chipsel); break;
        default: ret = 0xFF; LogPrint(LOG_FLASH | LOG_ODD, "UNKNOWN FLASH COMMAND %02X\n", flash->CurCmd); break;
    }
    // this is probably overkill but eh
    u16 cmdtmp;
    if (!ckd_add(&cmdtmp, flash->CmdLen, 1))
    {
        flash->CmdLen = cmdtmp;
    }
    flash->PrevChipSelect = chipsel;

    return ret;
}
