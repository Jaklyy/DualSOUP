#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>

#include "core/utils.h"
#include "core/console.h"
#include "gamepak.h"




bool GamePak_Init(GamePak* pak, GamePakCfg* cfg)
{
    pak->Inserted = cfg->Inserted;
    if (!pak->Inserted) return true;

    // INIT ROM
    size_t romsize = (size_t)1<<cfg->ROMChipSize;
    pak->ROMSize = romsize >> 1;

    if ((pak->ROM = malloc(romsize)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "Malloc failure in pak init\n");
        return false;
    }
    memset(pak->ROM, cfg->ROMPaddingByte, romsize);

    SDL_IOStream* rom;
    if ((rom = SDL_IOFromFile(cfg->ROMPath, "rb")) == NULL)
    {
        LogPrint(LOG_ALWAYS, "Could not open rom: %s\n%s", cfg->ROMPath, SDL_GetError());

        free(pak->ROM);
        return false;
    }

    size_t rombytes = SDL_ReadIO(rom, pak->ROM, romsize);
    if ((rombytes != romsize) && (SDL_GetIOStatus(rom) != SDL_IO_STATUS_EOF))
        LogPrint(LOG_ALWAYS, "ROM File read error: %s\n", SDL_GetError());

    LogPrint(LOG_ALWAYS, "Read %zu bytes of ROM\n", rombytes);

    if (!SDL_CloseIO(rom))
        LogPrint(LOG_ALWAYS, "ROM File close error: %s\n", SDL_GetError());

    // INIT RAM

    // TODO: EEPROM
    pak->EEPROMBegin = u32_max;

    // SRAM BUS
    pak->SRAMType = cfg->SRAMType;

    size_t sramsize = (size_t)1<<cfg->SRAMChipSize;
    pak->SRAMSize = sramsize;
    if (cfg->SRAMType != GamePakSRAM_None)
    {
        if ((pak->SRAM = malloc(sramsize)) == NULL)
        {
            LogPrint(LOG_ALWAYS, "Could not allocate RAM for Game Card SRAM\n");
            goto fail;
        }
        memset(pak->SRAM, 0xFF, sramsize);

        SDL_IOStream* sram;
        {
            pak->SavFolder = malloc(strlen(cfg->FolderPath)+sizeof(DSSavFolder));
            strcpy(pak->SavFolder, cfg->FolderPath);
            strcat(pak->SavFolder, DSSavFolder);

            char savpath[strlen(pak->SavFolder)+sizeof(DSSavFile)];
            strcpy(savpath, pak->SavFolder);
            strcat(savpath, DSSavFile);

            if ((sram = SDL_IOFromFile(savpath, "rb")) == NULL)
            {
                LogPrint(LOG_ALWAYS, "Could not open SRAM: %s\n%s", savpath, SDL_GetError());

                free(pak->SRAM);
                goto fail;
            }
        }

        size_t srambytes = SDL_ReadIO(sram, pak->SRAM, sramsize);
        if ((srambytes != sramsize) && (SDL_GetIOStatus(sram) != SDL_IO_STATUS_EOF))
            LogPrint(LOG_ALWAYS, "SRAM File read error: %s\n", SDL_GetError());

        LogPrint(LOG_ALWAYS, "Read %zu bytes of SRAM\n", srambytes);

        if (!SDL_CloseIO(sram))
            LogPrint(LOG_ALWAYS, "SRAM File close error: %s\n", SDL_GetError());

        if (false)
        {
            fail:
            free(pak->ROM);
            return false;
        }
    }

    switch (pak->SRAMType)
    {
    case GamePakSRAM_None: break;
    case GamePakSRAM_SRAM: break;
    case GamePakSRAM_Flash:
    {
        pak->FlashChipID[0] = cfg->FlashChipID & 0xFF;
        pak->FlashChipID[1] = (cfg->FlashChipID >> 8) & 0xFF;
        pak->FlashType = cfg->GamePakFlashType;
        break;
    }
    }
    return true;
}

u16 GamePak_ROMRead(GamePak* pak, u32 addr)
{
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
            return addr & 0xFFFF; // TODO: improve open bus
        }
        else // rom present
        {
            LogPrint(LOG_PAK|LOG_UNIMP, "Reading past end of Game Pak ROM? Addr: %06"PRIX32" Size: %06zX\n", addr, pak->ROMSize);
            return addr & 0xFFFF; // seems to return garbage? TODO: improve
        }
    }

    return pak->ROM[addr];
}

void GamePakSRAM_SetDirty(GamePak* pak)
{
    if (!pak->SRAMDirty)
    {
        pak->SRAMDirty = true;
        pak->SRAMDirtyCycle = SDL_GetPerformanceCounter() + (SDL_GetPerformanceFrequency() * 10);
    }
}

bool GamePakSRAM_FlushToDisk(GamePak* pak, u8** outbuf, u64* size, bool force)
{
    if (pak->SRAMDirty && (force || (SDL_GetPerformanceCounter() >= pak->SRAMDirtyCycle)))
    {
        *outbuf = pak->SRAM;
        *size = pak->SRAMSize;
        pak->SRAMDirty = false;
        return true;
    }
    return false;
}

void GamePak_Cleanup(GamePak* pak)
{
    switch(pak->SRAMType)
    {
    case GamePakSRAM_None: break;
    case GamePakSRAM_Flash: 
    case GamePakSRAM_SRAM:
        free(pak->SRAM);
        break;
    }
    free(pak->SavFolder);
    free(pak->ROM);
}

void GamePak_ROMWrite(GamePak* pak, u32 addr, u16 val)
{
    // speculation:
    // byte writes are either rejected by the interface, or mirrored? (or only update part of the halfword?)
    // game pak does not have a byte enable signal, so it likely can't support actual byte accesses.
    // word writes are either split into two halfword writes or one of them is ignored?

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
        if (addr == 0x5555)
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

                GamePakSRAM_SetDirty(pak);
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

                GamePakSRAM_SetDirty(pak);
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

                GamePakSRAM_SetDirty(pak);
                return;
            }
            else
            {
                pak->SRAM[addr] &= val; // functions as a bitwise AND...?
                pak->FlashCmdProg = GPFCmdProg_Begin;

                GamePakSRAM_SetDirty(pak);
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
            pak->SRAM[addr] = val;

            GamePakSRAM_SetDirty(pak);
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

#define romsel (pak->ROM_CSel)
#define ramsel (pak->RAM_CSel)
#define readsel (pak->Read_Sel)
#define writesel (pak->Write_Sel)
void GamePak_Step(BusGamePak* bus, GamePak* pak)
{
    if (!pak->Inserted) return;
    // toggle rom chipselect
    if (pak->ROM_CSel != bus->ROM_CSel)
    {
        pak->ROM_CSel = bus->ROM_CSel;

        if (romsel) // TODO: what the hell happens to the high address lanes, since those aren't reused for data out?
            pak->LatchedAddr_Full = bus->Addr_Full;
    }

    // toggle ram chipselect
    if (pak->RAM_CSel != bus->RAM_CSel)
    {
        pak->RAM_CSel = bus->RAM_CSel;
    }

    if (ramsel && romsel)
        LogPrint(LOG_PAK|LOG_UNIMP, "GAMEPAK: RAM AND ROM CHIPSEL BOTH ACTIVE; EXPLODE???\n");

    bool stepaddr = false;
    if (pak->Read_Sel != bus->Read_Sel)
    {
        pak->Read_Sel = bus->Read_Sel;

        if (readsel)
        {
            if (romsel)      bus->ROM_Data = GamePak_ROMRead(pak, pak->LatchedAddr_Full);
            else if (ramsel) bus->RAM_Data = GamePak_SRAMRead(pak, bus->Addr_Lo);
            else             LogPrint(LOG_PAK|LOG_UNIMP, "GAMEPAK: READ SIGNALED BUT NO CHIP SELECT HELD.\n");
        }
        else if (romsel) stepaddr = true;
    }

    if (pak->Write_Sel != bus->Write_Sel)
    {
        pak->Write_Sel = bus->Write_Sel;

        if (writesel)
        {
            if (romsel)      GamePak_ROMWrite(pak, pak->LatchedAddr_Full, bus->ROM_Data);
            else if (ramsel) GamePak_SRAMWrite(pak, bus->Addr_Lo, bus->RAM_Data);
            else             LogPrint(LOG_PAK|LOG_UNIMP, "GAMEPAK: WRITE SIGNALED BUT NO CHIP SELECT HELD.\n");
        }
        else if (romsel) stepaddr = true;
    }

    if ((readsel || writesel) && !(romsel || ramsel))
        LogPrint(LOG_PAK|LOG_UNIMP, "GAMEPAK: A DATA REQ ACTIVE BUT NO CHIP SELECT HELD; EXPLODE???\n");
    if (readsel && writesel)
        LogPrint(LOG_PAK|LOG_UNIMP, "GAMEPAK: READ AND WRITE SEL BOTH ACTIVE; EXPLODE???\n");

    // this does not step the upper address
    if (stepaddr) pak->LatchedAddr_Lo = (pak->LatchedAddr_Lo + 1) & 0xFFFF;
}


void GamePakBus_Reset(Console* sys)
{
    memset(&sys->BusGP, 0, sizeof(sys->BusGP));
    GamePak_Step(&sys->BusGP, &sys->GamePak);
}

void GamePakBus_KillBurst(Console* sys, timestamp now [[maybe_unused]], bool a9)
{
    if (a9 == sys->ExtMemCR_Shared.GBAPakA7Access) return;

    BusGamePak* bus = &sys->BusGP;
    bus->BurstActive = false;
    bus->ROM_CSel = false;
}

void GamePakBus_ROMHandler(Console* sys, timestamp now, u8 waitstate, bool agb, bool a9)
{
    u8 ns;
    u8 seq;
    if (agb)
    {
        switch(waitstate)
        {
        case 0:
            ns  = GamePak_WaitStates[2][sys->ExtMemCR_AGB.PakROMTimingsNS0];
            seq = GamePak_WaitStates[3][sys->ExtMemCR_AGB.PakROMTimingsS0];
            break;
        case 1:
            ns  = GamePak_WaitStates[2][sys->ExtMemCR_AGB.PakROMTimingsNS1];
            seq = GamePak_WaitStates[4][sys->ExtMemCR_AGB.PakROMTimingsS1];
            break;
        case 2:
            ns  = GamePak_WaitStates[2][sys->ExtMemCR_AGB.PakROMTimingsNS2];
            seq = GamePak_WaitStates[5][sys->ExtMemCR_AGB.PakROMTimingsS2];
            break;
        }
    }
    else
    {
        ns  = GamePak_WaitStates[0][a9 ? sys->ExtMemCR_9.GBAROMTimingsNS  : sys->ExtMemCR_7.GBAROMTimingsNS];
        seq = GamePak_WaitStates[1][a9 ? sys->ExtMemCR_9.GBAROMTimingsSeq : sys->ExtMemCR_7.GBAROMTimingsSeq];
    }
    BusReq* req = (a9 ? &sys->Bus9.PipeFIFO[sys->Bus9.ReqActivePtr] : &sys->Bus7.PipeFIFO[sys->Bus7.ReqActivePtr]);
    BusGamePak* bus = &sys->BusGP;
    GamePak* pak = &sys->GamePak;

    u32 rdata = 0;
    if (!agb && (a9 == sys->ExtMemCR_Shared.GBAPakA7Access)) // unmapped
    {
        failure:
    }
    else
    {
        // checkme: does the gamepak interface support 32 bit writes?
        // i would assume so because of the ram cart addons, but that's not a guarantee.
        u32 addr = (req->Addr & (~req->Size /* dumb way to align addr for words */)) >> 1; // 16 bit addressable

        if (!(addr & 0xFFFF)) // 128 KiB boundaries are enforced by the gamepak interface
            bus->BurstActive = false;

        if (req->Write)
        {
            if ((req->Size == HSIZE_8) && !agb) // byte writes are not permitted (nds only?)
                goto failure;

            for (int i = 0; i < req->Size; i++) // 32 bit is 2, 16 bit is 1; it just works:tm:
            {
                u16 wrdata = ROR32(req->WrData, (addr&2)*8) & 0xFFFF;

                if (!bus->BurstActive) // nseq
                {
                    // pull cs hi (in case it was low before?)
                    // note: im not sure if anything on the arm7 bus can realistically trigger two ns cycles back to back
                    // speculation: the lack of time to pull cs high could be why dma does weird shit to game pak?
                    // ie. fixed/decrementing bursts / rom -> rom transfers
                    // ...probably not that sounds like a really stupid problem to have and not fix
                    // unless they just didn't give a shit, which is possible...
                    bus->ROM_CSel = false;
                    GamePak_Step(bus, pak);
                    // pull cs lo (latch addr?)
                    bus->Addr_Full = addr & 0xFFFFFF;
                    bus->ROM_CSel = true;
                    GamePak_Step(bus, pak);
                    // pull wr low (start write?)
                    bus->ROM_Data = wrdata;
                    bus->Write_Sel = true;
                    GamePak_Step(bus, pak);
                    // pull wr high (incr addr?)
                    bus->Write_Sel = false;
                    GamePak_Step(bus, pak);

                    now += DSClkSys(ns, agb);
                    bus->BurstActive = true;
                }
                else
                {
                    // pull wr low (start write?)
                    bus->ROM_Data = wrdata;
                    bus->Write_Sel = true;
                    GamePak_Step(bus, pak);
                    // pull wr high (incr addr?)
                    bus->Write_Sel = false;
                    GamePak_Step(bus, pak);

                    now += DSClkSys(seq, agb);
                }
            }
        }
        else
        {
            for (int i = 0; i < ((req->Size == HSIZE_32) ? 2 : 1); i++)
            {
                if (!bus->BurstActive) // nseq
                {
                    // pull cs hi (in case it was low before?)
                    // note: im not sure if anything on the arm7 bus can realistically trigger two ns cycles back to back
                    // speculation: the lack of time to pull cs high could be why dma does weird shit to game pak?
                    // ie. fixed/decrementing bursts / rom -> rom transfers
                    // ...probably not that sounds like a really stupid problem to have and not fix
                    // unless they just didn't give a shit, which is possible...
                    bus->ROM_CSel = false;
                    GamePak_Step(bus, pak);
                    // pull cs lo (latch addr?)
                    bus->Addr_Full = addr & 0xFFFFFF;
                    bus->ROM_CSel = true;
                    GamePak_Step(bus, pak);
                    // pull rd low (start read?)
                    bus->Read_Sel = true;
                    GamePak_Step(bus, pak);
                    // pull rd high (incr addr?)
                    bus->Read_Sel = false;
                    GamePak_Step(bus, pak);
                    // read data off bus
                    rdata |= bus->ROM_Data;
                    rdata = ROR32(rdata, 16); // this results in data being in the right lanes for 32 bit loads

                    if (!agb || !sys->ExtMemCR_AGB.PakPrefetcher) // TODO: prefetcher
                        now += DSClkSys(ns, agb);

                    bus->BurstActive = true;
                }
                else
                {
                    // pull rd low (start read?)
                    bus->Read_Sel = true;
                    GamePak_Step(bus, pak);
                    // pull rd high (incr addr?)
                    bus->Read_Sel = false;
                    GamePak_Step(bus, pak);
                    // read data off bus
                    rdata |= bus->ROM_Data;
                    rdata = ROR32(rdata, 16); // this results in data being in the right lanes for 32 bit loads

                    if (!agb || !sys->ExtMemCR_AGB.PakPrefetcher) // TODO: prefetcher
                        now += DSClkSys(seq, agb);
                }
            }
            if (req->Size != HSIZE_32)
                rdata |= rdata >> 16; // rdata is in the high portion after earlier rotate
            //printf("%08X %08X\n", req->Addr, rdata);
        }
    }
    Bus_TransferPostSetup(sys, rdata, !req->Write, now, false, a9);
}

void GamePakBus_RAMHandler(Console* sys, timestamp now, bool agb, bool a9)
{
    u8 ns;
    if (agb) ns = GamePak_WaitStates[2][sys->ExtMemCR_AGB.PakRAMTimings];
    else     ns = GamePak_WaitStates[0][a9 ? sys->ExtMemCR_9.GBARAMTimings : sys->ExtMemCR_7.GBARAMTimings];

    BusReq* req = (a9 ? &sys->Bus9.PipeFIFO[sys->Bus9.ReqActivePtr] : &sys->Bus7.PipeFIFO[sys->Bus7.ReqActivePtr]);
    BusGamePak* bus = &sys->BusGP;
    GamePak* pak = &sys->GamePak;

    u32 rdata = 0;

    // note: 8 bit bus, only supports byte r/w, does not ignore low bits of address for larger accesses.
    if (!agb && (a9 == sys->ExtMemCR_Shared.GBAPakA7Access))
    {
        failure:
    }
    else
    {
        u32 addr = req->Addr & 0xFFFF; // byte addressable

        if (req->Write)
        {
            if ((req->Size != HSIZE_8) && !agb) // only byte writes are permitted (nds only)
                goto failure;

            u8 wrdata = ROR32(req->WrData, (addr&3)*8) & 0xFF;

            // pull cs lo (sample addr?)
            bus->Addr_Lo = addr;
            bus->RAM_Data = wrdata;
            bus->RAM_CSel = true;
            GamePak_Step(bus, pak);
            // pull wr low (sample data?)
            bus->Write_Sel = true;
            GamePak_Step(bus, pak);
            // pull wr high (???)
            bus->Write_Sel = false;
            GamePak_Step(bus, pak);
            // pull cs hi (complete access ???)
            bus->RAM_CSel = false;
            GamePak_Step(bus, pak);
        }
        else
        {
            // pull cs lo (sample addr?)
            bus->Addr_Lo = addr;
            bus->RAM_CSel = true;
            GamePak_Step(bus, pak);
            // pull rd low (sample data?)
            bus->Read_Sel = true;
            GamePak_Step(bus, pak);
            // pull rd high (???)
            bus->Read_Sel = false;
            GamePak_Step(bus, pak);
            // pull cs hi (complete access ???)
            bus->RAM_CSel = false;
            GamePak_Step(bus, pak);
            // read data off bus
            rdata = bus->RAM_Data;
            rdata |= (rdata << 8) | (rdata << 16) | (rdata << 24); // mirror across all lanes
        }
        now += DSClkSys(ns, agb);
    }
    Bus_TransferPostSetup(sys, rdata, !req->Write, now, false, a9);
}
