#include <stdio.h>
#include <stdlib.h>

#include "gamepak.h"
#include "core/utils.h"




void GamePak_Init(GamePak* pak)
{
    // TODO: expose config
    // dummy
    pak->EEPROMBegin = u32_max;
}

u16 GamePak_ROMRead(GamePak* pak, u32 addr)
{
    addr /= 2; // ROM is halfword indexable
    addr &= ((1<<24)-1); // 24 bit address bus

    // check if access is to GPIO ports
    if (((addr <= 0x64) && (addr >= 0x62)) // address range check
        && (pak->GPIO.Present && pak->GPIO.ReadEnable))
    {
        LogPrint(LOG_PAK|LOG_UNIMP, "UNIMP: Game Pak GPIO Read. %06"PRIX32"\n", addr);
        return 0;// TODO: GPIO
    }

    // check if we're in the eeprom range
    if (addr >= pak->EEPROMBegin)
    {
        LogPrint(LOG_PAK|LOG_UNIMP, "UNIMP: Game Pak EEPROM Read. Addr: %06"PRIX32" EEPBeg: %06"PRIX32"\n", addr, pak->EEPROMBegin);
        return 0xFFFF; // TODO EEPROM
    }

    // actual rom access

    // TODO: ROM banking?
    if (addr >= pak->ROMSize)
    {
        if (pak->ROMSize == 0) // rom absent
        {
            return 0xFFFF; // TODO: open bus
        }
        else // rom present
        {
            LogPrint(LOG_PAK|LOG_UNIMP, "Reading past end of Game Pak ROM? Addr: %06"PRIX32" Size: %06zX\n", addr, pak->ROMSize);
            return 0xFFFF; // seems to return garbage?
        }
    }

    return pak->ROM[addr];
}

void GamePak_ROMWrite(GamePak* pak, u32 addr, u16 val)
{
    // speculation:
    // byte writes are either rejected by the interface, or mirrored? (or only update part of the halfword?)
    // game pak does not have a byte enable signal, so it likely can't support actual byte accesses.
    // word writes are either split into two halfword writes or one of them is ignored?
    addr /= 2; // ROM is halfword indexable
    addr &= ((1<<24)-1); // 24 bit address bus

    // check if access is to GPIO ports
    if (((addr <= 0x64) && (addr >= 0x62)) // address range check
        && (pak->GPIO.Present))
    {
        LogPrint(LOG_PAK|LOG_UNIMP, "UNIMP: Game Pak GPIO Write. Addr:%06"PRIX32", Val:%04"PRIX16"\n", addr, val);
        return; // TODO: GPIO
    }

    // check if we're in the eeprom range
    if (addr >= pak->EEPROMBegin)
    {
        LogPrint(LOG_PAK|LOG_UNIMP, "UNIMP: Game Pak EEPROM Write. Addr:%06"PRIX32" EEPBeg:%06"PRIX32", Val:%04"PRIX16"\n", addr, pak->EEPROMBegin, val);
        return; // TODO EEPROM
    }

    // its *read only* memory, dingus
    LogPrint(LOG_PAK|LOG_ODD, "Writing to Game Pak ROM? Addr:%06"PRIX32" Size:%08zX, Val:%04"PRIX16"\n", addr, pak->ROMSize, val);
    return;
}

u8 GamePak_SRAMRead(GamePak* pak, const u16 addr)
{
    if (pak->FlashChipIDMode)
    {
        if (addr > 1)
        {
            LogPrint(LOG_PAK|LOG_UNIMP, "What happens when you read an addr > 1 in Game Pak flash chip id mode? Addr:%05"PRIX16"\n", addr);
        }
        return pak->FlashChipID[addr & 1];
    }
    size_t actualaddr = addr | pak->SRAMBankOffs;
    if (actualaddr >= pak->SRAMSize)
    {
        if (pak->SRAMSize == 0) // sram absent
        {
            return 0xFF;
        }
        else // sram present
        {
            LogPrint(LOG_PAK|LOG_UNIMP, "Reading past end of Game Pak SRAM? Addr:%05zX Size:%05zX\n", actualaddr, pak->SRAMSize);
            return 0xFF;
        }
    }
    return pak->SRAM[actualaddr];
}

void GamePak_FlashCmdSubmit(GamePak* pak, const u16 addr, const u8 val)
{
    size_t actualaddr = addr | pak->SRAMBankOffs;
    switch(pak->FlashCmdProg)
    {
    case GPFCmdProg_Begin:
    {
        if ((addr == 0x5555))
        {
            if (val == 0xAA)
            {
                pak->FlashCmdProg = GPFCmdProg_Begin2;
                return;
            }
            else if (val == GPFCmd_TerminateCmd)
            {
                LogPrint(LOG_PAK|LOG_UNIMP, "Game Pak Macronix Flash: Terminate Command? Software thinks cmd failed?\n");
                pak->FlashCmdProg = GPFCmdProg_Begin;
                return;
            }
        }
        break;
    }
    case GPFCmdProg_Begin2:
    {
        if ((addr == 0x2AAA) && (val == 0x55))
        {
            pak->FlashCmdProg = (pak->FlashErasePrefixed ? GPFCmdProg_EraseCmdEntry : GPFCmdProg_CmdEntry); // checkme: idk how this actually works??
            pak->FlashErasePrefixed = false;
            return;
        }
        break;
    }
    case GPFCmdProg_CmdEntry:
    {
        if (addr == 0x5555)
        {
            switch((GamePakFlashCmds)val)
            {
            case GPFCmd_ErasePrefix:
            {
                pak->FlashErasePrefixed = true;
                pak->FlashCmdProg = GPFCmdProg_Begin;
                return;
            }
            case GPFCmd_ChipIDEnable:
            {
                pak->FlashChipIDMode = true;
                pak->FlashCmdProg = GPFCmdProg_Begin;
                return;
            }
            case GPFCmd_TerminateCmd:
            {
                pak->FlashChipIDMode = false;
                pak->FlashCmdProg = GPFCmdProg_Begin;
                return;
            }
            case GPFCmd_Write:
            {
                pak->FlashCmdProg = GPFCmdProg_WriteDataBase;
                return;
            }
            case GPFCmd_SwitchBank:
            {
                pak->FlashCmdProg = GPFCmdProg_BankEntry;
                return;
            }
            default: break;
            }
        }
        break;
    }
    case GPFCmdProg_EraseCmdEntry: // erase queued?
    {
        switch((GamePakFlashCmds)val)
        {
        case GPFCmd_EraseAll:
        {
            if (addr == 0x5555)
            {
                pak->FlashCmdProg = GPFCmdProg_Begin;
                // TODO: timings
                memset(pak->SRAM, 0xFF, pak->SRAMSize);
                return;
            }
            break;
        }
        case GPFCmd_EraseSector:
        {
            if ((actualaddr < pak->SRAMSize) && (pak->FlashType != GamePakFlash_Atmel))
            {
                pak->FlashCmdProg = GPFCmdProg_Begin;
                // TODO: timings
                memset(&pak->SRAM[addr & 0xF000], 0xFF, KiB(4));
                return;
            }
            break;
        }
        default: break;
        }
        break;
    }
    case GPFCmdProg_BankEntry: // bank switch
    {
        if (val * KiB(64) < pak->SRAMSize)
        {
            pak->SRAMBankOffs = val * KiB(64);
            pak->FlashCmdProg = GPFCmdProg_Begin;
            return;
        }
        break;
    }
    case GPFCmdProg_WriteDataBase ... (GPFCmdProg_WriteDataBase + 127): // expecting data for a write
    {
        if (actualaddr < pak->SRAMSize)
        {
            if (pak->FlashType == GamePakFlash_Atmel) // erase and write 128 bytes
            {
                // misalignment handling is a guess
                pak->SRAM[(actualaddr & 0xFF80) + (pak->FlashCmdProg-GPFCmdProg_WriteDataBase)] = val;
                if (pak->FlashCmdProg == GPFCmdProg_WriteDataBase+127)
                {
                    pak->FlashCmdProg = GPFCmdProg_Begin;
                }
                else pak->FlashCmdProg++;
                return;
            }
            else
            {
                pak->SRAM[addr] &= val; // functions as a bitwise AND...?
                pak->FlashCmdProg = GPFCmdProg_Begin;
                return;
            }
        }
        break;
    }
    }
    LogPrint(LOG_PAK|LOG_UNIMP, "Nonsense Game Pak Flash Cmd Submission: Addr:%05zX Val:%02"PRIX8" Prog:%"PRIi8" Type:%"PRIi8"\n", actualaddr, val, pak->FlashCmdProg, pak->FlashType);
    pak->FlashCmdProg = GPFCmdProg_Begin;
    pak->FlashErasePrefixed = false;
}

void GamePak_SRAMWrite(GamePak* pak, const u16 addr, const u8 val)
{
    switch(pak->SRAMType)
    {
    case GamePakSRAM_None:
    {
        break;
    }
    case GamePakSRAM_SRAM:
    {
        if (addr >= pak->SRAMSize)
        {
            LogPrint(LOG_PAK|LOG_UNIMP, "Writing past end of Game Pak SRAM? Addr: %05"PRIX16" Val:%02"PRIX8" Size: %05zX\n", addr, val, pak->SRAMSize);
        }
        else
        {
            pak->SRAM[pak->SRAMSize] = val;
        }
        break;
    }
    case GamePakSRAM_Flash:
    {
        GamePak_FlashCmdSubmit(pak, addr, val);
        break;
    }
    }
}
