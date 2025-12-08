#include <stdckdint.h>
#include <stdbit.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "flash.h"




bool Flash_Init(Flash* flash, FILE* ram, u64 size, bool writeprot, const int id, const char* str)
{
    if (ram != NULL)
    {
        // get size from submitted file
        fseek(ram, 0, SEEK_END);
        size = ftell(ram);
        fseek(ram, 0, SEEK_SET);
    }
    if (stdc_count_ones(size) != 1)
    {
        LogPrint(LOG_ALWAYS, "FATAL: %s size %li is not a power of 2!\n", str, size);
        return false;
    }
    if (size-1 > 0xFFFFFF)
    {
        LogPrint(LOG_ALWAYS, "FATAL: %s too big! must be <= 16 MiB; currently %li\n", str, size);
        return false;
    }
    if (size < 256)
    {
        LogPrint(LOG_ALWAYS, "FATAL: %s too small! must be >= 256 bytes; currently %li\n", str, size);
        return false;
    }

    flash->RAMSize = size;
    flash->WriteProt = writeprot ? ~((size/4)-1) : 0;
    flash->RAM = malloc(size);
    if (flash->RAM == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: %s allocation failed\n", str);
        return false;
    }

    if (ram == NULL)
    {
        memset(flash->RAM, 0, flash->RAMSize);
    }
    else if (fread(flash->RAM, flash->RAMSize, 1, ram) != 1)
    {
        LogPrint(LOG_ALWAYS, "FATAL: %s read failed\n", str);
        free(flash->RAM);
        flash->RAM = nullptr;
        return false;
    }

    flash->ID[0] = (id>> 0) & 0xFF;
    flash->ID[1] = (id>> 8) & 0xFF;
    flash->ID[2] = (id>>16) & 0xFF;
    return true;
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
    if (flash->PowerDown || chipsel) return 0xFF;
    flash->WriteEnabled = true;
    return 0;
}

u8 Flash_WriteDisable(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (flash->PowerDown || chipsel) return 0xFF;
    flash->WriteEnabled = false;
    return 0;
}

u8 Flash_EnterDeepPowerDown(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (chipsel || flash->Busy) return 0xFF;
    flash->PowerDown = true;
    return 0;
}

u8 Flash_ExitDeepPowerDown(Flash* flash, const bool chipsel)
{
    // CHECKME: does this work with chipsel held?
    if (chipsel || flash->Busy) return 0xFF;
    flash->PowerDown = false;
    return 0;
}

u8 Flash_ReadID(Flash* flash)
{
    if (flash->PowerDown || flash->Busy) return 0xFF;

    if (flash->CmdLen < 3)
    {
        return flash->ID[flash->CmdLen];
    }
    else return 0; // checkme?
}

u8 Flash_ReadStatus(Flash* flash)
{
    if (flash->PowerDown) return 0xFF;

    return (flash->WriteEnabled << 1) | flash->Busy;
}

u8 Flash_ReadData(Flash* flash, const u8 val)
{
    if (flash->PowerDown || flash->Busy) return 0xFF;

    if ((flash->CmdLen < 4))
    {
        if (flash->CmdLen != 0)
        {
            flash->CurAddr |= val << ((flash->CmdLen-1)*8);
        }
        return 0;
    }
    else
    {
        // checkme: how does it actually do masking?
        return flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)];
    }
}

u8 Flash_ReadDataFast(Flash* flash, const u8 val)
{
    if (flash->PowerDown || flash->Busy) return 0xFF;

    if ((flash->CmdLen < 5))
    {
        if (flash->CmdLen != 0 && flash->CmdLen != 4)
        {
            flash->CurAddr |= val << ((flash->CmdLen-1)*8);
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
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy) return 0xFF;

    if ((flash->CmdLen < 4))
    {
        if (flash->CmdLen != 0)
        {
            flash->CurAddr |= val << ((flash->CmdLen-1)*8);
        }
        return 0;
    }
    else if (flash->CurAddr & flash->WriteProt)
    {
        if ((flash->CmdLen - 4) >= 256)
        {
            flash->CmdLen = 256 + 4;
        }

        // checkme: how does it actually do masking?
        flash->DataBuffer[flash->WritePos++] = val;

        flash->WritePos &= 0xFF;

        if (!chipsel)
        {
            // CHECKME: is this correct behavior for buffer overflow?
            for (int i = 0; i < flash->CmdLen - 4; i++)
            {
                flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)] = flash->DataBuffer[i];

                // wrap value to page boundary
                if (!(flash->CurAddr & 0xFF))
                    flash->CurAddr -= 256;
            }
        }
    }
    return 0;
}

u8 Flash_PageProgram(Flash* flash, const u8 val, const bool chipsel)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy) return 0xFF;

    if ((flash->CmdLen < 4))
    {
        if (flash->CmdLen != 0)
        {
            flash->CurAddr |= val << ((flash->CmdLen-1)*8);
        }
        return 0;
    }
    else if (flash->CurAddr & flash->WriteProt)
    {
        if (chipsel)
        {
            if ((flash->CmdLen - 4) >= 256)
            {
                flash->CmdLen = 256 + 4;
            }

            // checkme: how does it actually do masking?
            flash->DataBuffer[flash->WritePos++] = val;

            flash->WritePos &= 0xFF;
        }
        else
        {
            // CHECKME: is this correct behavior for buffer overflow?
            for (int i = 0; i < flash->CmdLen - 4; i++)
            {
                // checkme: what does this actually do...?
                flash->RAM[flash->CurAddr++ & (flash->RAMSize-1)] &= ~flash->DataBuffer[i];

                // wrap value to page boundary
                if (!(flash->CurAddr & 0xFF))
                    flash->CurAddr -= 256;
            }
        }
    }
    return 0;
}

u8 Flash_PageErase(Flash* flash, const u8 val)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy) return 0xFF;

    // TODO chipsel?
    if ((flash->CmdLen < 4))
    {
        // just dont set the low byte tbh
        if (flash->CmdLen > 1)
        {
            flash->CurAddr |= val << ((flash->CmdLen-1)*8);
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
    return 0;
}

u8 Flash_SectorErase(Flash* flash, const u8 val)
{
    if (flash->PowerDown || !flash->WriteEnabled || flash->Busy) return 0xFF;

    // TODO chipsel?
    if ((flash->CmdLen < 4))
    {
        // just dont set the low bytes tbh
        if (flash->CmdLen > 2)
        {
            flash->CurAddr |= val << ((flash->CmdLen-1)*8);
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
        case 0xDB: ret = Flash_PageErase(flash, val); break;
        case 0xD8: ret = Flash_SectorErase(flash, val); break;
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
