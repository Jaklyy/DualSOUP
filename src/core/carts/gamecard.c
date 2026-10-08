#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <SDL3/SDL_iostream.h>

#include "gamecard.h"
#include "core/console.h"
#include "core/scheduler.h"



bool GameCard_Init(GameCard* card, const GameCardConfig* cfg, u8* bios7)
{
    size_t romsize = (size_t)1<<cfg->ROMChipSize;
    card->RomSize = romsize;
    card->ChipID = cfg->ROMChipID;
    card->ChipID &= 0x7FFFFFFF; // TODO: IMPLEMENT PROTOCOL VARIANT

    if ((card->ROM = malloc(romsize)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "Malloc failure in card init\n");
        return false;
    }
    memset(card->ROM, cfg->ROMPaddingByte, romsize);

    SDL_IOStream* rom;
    if ((rom = SDL_IOFromFile(cfg->ROMPath, "rb")) == NULL)
    {
        LogPrint(LOG_ALWAYS, "Could not open rom: %s\n%s", cfg->ROMPath, SDL_GetError());

        free(card->ROM);
        return false;
    }

    size_t rombytes = SDL_ReadIO(rom, card->ROM, romsize);
    if ((rombytes != romsize) && (SDL_GetIOStatus(rom) != SDL_IO_STATUS_EOF))
        LogPrint(LOG_ALWAYS, "ROM File read error: %s\n", SDL_GetError());

    LogPrint(LOG_ALWAYS, "Read %zu bytes of ROM\n", rombytes);

    if (!SDL_CloseIO(rom))
        LogPrint(LOG_ALWAYS, "ROM File close error: %s\n", SDL_GetError());

    memcpy(card->Key1, &bios7[0x30], sizeof(card->Key1));

    card->SPIType = cfg->SPIBusType;
    card->SRAMType = cfg->SRAMChipType;

    GCSRAM* sram;
    switch(cfg->SPIBusType)
    {
    case GameCard_SPIBus_None:
        return true;
    case GameCard_SPIBus_DirectSRAM:
        sram = &card->SPI.SRAM;
        break;
    case GameCard_SPIBus_InfraredHLE:
        sram = &card->SPI.IRhle.SRAM;
        break;
    }

    size_t sramsize = (size_t)1<<cfg->SRAMChipSize;
    u8* srambuffer;
    if (cfg->SRAMChipType != GameCard_SRAMChip_None)
    {
        if ((srambuffer = malloc(sramsize)) == NULL)
        {
            LogPrint(LOG_ALWAYS, "Could not allocate RAM for Game Card SRAM\n");
            goto fail;
        }
        memset(srambuffer, 0xFF, sramsize);

        SDL_IOStream* sram;
        if ((sram = SDL_IOFromFile(cfg->SRAMPath, "rb")) == NULL)
        {
            LogPrint(LOG_ALWAYS, "Could not open SRAM: %s\n%s", cfg->SRAMPath, SDL_GetError());

            free(srambuffer);
            goto fail;
        }

        size_t srambytes = SDL_ReadIO(sram, srambuffer, sramsize);
        if ((srambytes != sramsize) && (SDL_GetIOStatus(sram) != SDL_IO_STATUS_EOF))
            LogPrint(LOG_ALWAYS, "SRAM File read error: %s\n", SDL_GetError());

        LogPrint(LOG_ALWAYS, "Read %zu bytes of SRAM\n", srambytes);

        if (!SDL_CloseIO(sram))
            LogPrint(LOG_ALWAYS, "SRAM File close error: %s\n", SDL_GetError());

        if (false)
        {
            fail:
            free(card->ROM);
            return false;
        }
    }

    switch(cfg->SRAMChipType)
    {
    case GameCard_SRAMChip_None:
        return true;
    case GameCard_SRAMChip_Flash24BitAddr:
        Flash_Init(&sram->Flash, srambuffer, sramsize, false, cfg->FlashChipID);
        return true;
    case GameCard_SRAMChip_EEPROM9BitAddr:
    case GameCard_SRAMChip_EEPROM16BitAddr:
    case GameCard_SRAMChip_EEPROM24BitAddr:
        EEPROM_Init(&sram->EEP, srambuffer, sramsize, cfg->SRAMChipType-GameCard_SRAMChip_EEPROM9BitAddr+1, 0);
        return true;
    }
}

u8 GameCardSRAM_CmdSend(GCSRAM* self, GameCard_SRAMChip sram, u8 val, bool chipsel)
{
    switch(sram)
    {
    case GameCard_SRAMChip_None:
        return 0xFF; // checkme
    case GameCard_SRAMChip_Flash24BitAddr:
        return Flash_CMDSend((Flash*)self, val, chipsel);
    case GameCard_SRAMChip_EEPROM9BitAddr
    ... GameCard_SRAMChip_EEPROM24BitAddr:
        return EEPROM_CMDSend((EEPROM*)self, val, chipsel);
    }
}

u8 GameCardSPI_CmdSend(GCSPI* self, GameCard_SPIBus spi, GameCard_SRAMChip sram, u8 val, bool chipsel)
{
    switch(spi)
    {
    case GameCard_SPIBus_None:
        return 0xFF; // checkme
    case GameCard_SPIBus_DirectSRAM:
        return GameCardSRAM_CmdSend(&self->SRAM, sram, val, chipsel);
    case GameCard_SPIBus_InfraredHLE:
        return IRhle_CMDSend((IRhle*)self, sram, val, chipsel);
    }
}

void GameCardSRAM_Cleanup(GCSRAM* self, GameCard_SRAMChip sram)
{
    switch(sram)
    {
    case GameCard_SRAMChip_None: return;
    case GameCard_SRAMChip_Flash24BitAddr:
        return Flash_Cleanup((Flash*)self);
    case GameCard_SRAMChip_EEPROM9BitAddr
    ... GameCard_SRAMChip_EEPROM24BitAddr:
        return EEPROM_Cleanup((EEPROM*)self);
    }
}

void GameCard_Cleanup(GameCard* card)
{
    switch(card->SPIType)
    {
    case GameCard_SPIBus_None: break;
    case GameCard_SPIBus_DirectSRAM:
        GameCardSRAM_Cleanup(&card->SPI.SRAM, card->SRAMType); break;
    case GameCard_SPIBus_InfraredHLE:
        IRhle_Cleanup((IRhle*)&card->SPI, card->SRAMType); break;
    }
    free(card->ROM);
    card->ROM = nullptr;
}

// TODO: reset func?

// this is just ripped from melonds
void Key1_Encrypt(GameCard* card, u32* data)
{
    u32 y = data[0];
    u32 x = data[1];
    u32 z;

    for (u32 i = 0x0; i <= 0xF; i++)
    {
        z = card->Key1[i] ^ x;
        x =  card->Key1[0x012 +  (z >> 24)        ];
        x += card->Key1[0x112 + ((z >> 16) & 0xFF)];
        x ^= card->Key1[0x212 + ((z >>  8) & 0xFF)];
        x += card->Key1[0x312 +  (z        & 0xFF)];
        x ^= y;
        y = z;
    }

    data[0] = x ^ card->Key1[0x10];
    data[1] = y ^ card->Key1[0x11];
}

// this is just ripped from melonds
void Key1_Decrypt(GameCard* card, u32* data)
{
    u32 y = data[0];
    u32 x = data[1];
    u32 z;

    for (u32 i = 0x11; i >= 0x2; i--)
    {
        z = card->Key1[i] ^ x;
        x =  card->Key1[0x012 +  (z >> 24)        ];
        x += card->Key1[0x112 + ((z >> 16) & 0xFF)];
        x ^= card->Key1[0x212 + ((z >>  8) & 0xFF)];
        x += card->Key1[0x312 +  (z        & 0xFF)];
        x ^= y;
        y = z;
    }

    data[0] = x ^ card->Key1[0x1];
    data[1] = y ^ card->Key1[0x0];
}

// this is just ripped from melonds
void Key1_Apply(GameCard* card, u32* code, u32 mod)
{
    Key1_Encrypt(card, &code[1]);
    Key1_Encrypt(card, &code[0]);

    u32 temp[2] = {0,0};

    for (u32 i = 0; i <= 0x11; i++)
    {
        card->Key1[i] ^= bswap(code[i % mod]);
    }
    for (u32 i = 0; i <= 0x410; i+=2)
    {
        Key1_Encrypt(card, temp);
        card->Key1[i  ] = temp[1];
        card->Key1[i+1] = temp[0];
    }
}

// this is just ripped from melonds
void GameCardMisc_InitKey1(GameCard* card)
{
    u32 code[3] = {card->ROM[0xC/4], card->ROM[0xC/4]>>1 ,card->ROM[0xC/4]<<1};
    Key1_Apply(card, code, 2);
    Key1_Apply(card, code, 2);
    card->Mode = Key1;
}

u32 GameCardMisc_ROMReadHandler(GameCard* card)
{
    u32 ret = card->ROM[card->Address/4];
    card->Address += 4;
    // force it to stay within one 4 KiB area.
    if (!(card->Address & (KiB(4)-1)))
    {
        if (card->NumWords) LogPrint(LOG_CARD, "Game Card read wrapping.\n");
        card->Address -= KiB(4);
    }

    return ret;
}

u32 GameCardMisc_ROMReadSecureAreaHandler(GameCard* card)
{
    u32 ret = card->ROM[(0x8000+card->Address)/4];
    card->Address += 4;
    card->Address &= 0x1FF;

    return ret;
}

u32 GameCardMisc_ReadSecureAreaHandler(GameCard* card)
{
    // TODO: what does this actually do???
    u32 ret = card->ROM[card->Address/4];
    card->Address += 4;
    return ret;
}

u32 GameCardMisc_UnencIDReadHandler(GameCard* card)
{
    return card->ChipID;
}

u32 GameCardMisc_UnencHeaderHandler(GameCard* card)
{
    card->Address &= 0xFFF;
    u32 ret = card->ROM[card->Address/4];
    card->Address += 4;
    return ret;
}

u32 GameCardMisc_InvalidCmdHandler([[maybe_unused]] GameCard* card)
{
    return 0xFFFFFFFF; // idk
}

void* GameCardMisc_ROMCommandHandler(Console* sys, const bool a9)
{
    GameCard* card = &sys->GameCard;
    u64 cmd = sys->GCCommandPort[a9].Raw;
    switch(card->Mode)
    {
        case Unenc:
        {
            switch(cmd & 0xFF)
            {
                case 0x9F:
                    // High Z
                    return GameCardMisc_InvalidCmdHandler;
                case 0x00:
                    // header
                    card->Address = (bswap(cmd) >> 24) & ~3;
                    return GameCardMisc_UnencHeaderHandler;
                case 0x90:
                    // chip id
                    return GameCardMisc_UnencIDReadHandler;
                case 0x3C:
                    // active key 1
                    GameCardMisc_InitKey1(card);
                    return GameCardMisc_InvalidCmdHandler; // idk?
            }
            break;
        }
        case Key1:
        {
            cmd = bswap(cmd);
            u32 bleh[2] = {cmd & 0xFFFFFFFF, cmd >> 32};
            Key1_Decrypt(card, bleh); // type punning...
            cmd = bleh[0] | (u64)bleh[1] << 32;
            cmd = bswap(cmd);

            switch(cmd & 0xF0)
            {
                case 0x40:
                    return GameCardMisc_InvalidCmdHandler; // idk?
                case 0x10:
                    return GameCardMisc_UnencIDReadHandler;
                case 0x20:
                    card->Address = ((bswap(cmd) >> 44) & 0x7) << 12; // checkme: decoding on this seems weird; are the nibbles swapped?
                    return GameCardMisc_ReadSecureAreaHandler;
                case 0xA0:
                    card->Mode = Key2;
                    return GameCardMisc_InvalidCmdHandler; // idk?
            }
            break;
        }
        case Key2:
        {
            //printf("%016lX %i\n", bswap(cmd), a9);
            switch(cmd & 0xFF)
            {
                case 0xB7:
                {
                    // data
                    //printf("%08lX %08X %08X\n", (bswap(cmd) >> 24) & 0xFFFFFFFF, sys->GCROMCR[a9].Raw, sys->GCSPICR[a9].Raw);
                    card->Address = (bswap(cmd) >> 24);
                    if (card->Address >= card->RomSize) LogPrint(LOG_CARD, "Game Card address space wrapping: %08X %08X\n", card->Address, card->RomSize);
                    card->Address &= (card->RomSize-4); // subtract 4 as a weird way to handle masking out bottom bits as well.
                    if (card->Address < 0x8000)
                    {
                        // secure area is rerouted to the 512 bytes above it
                        card->Address &= 0x1FF;
                        LogPrint(LOG_CARD, "Game Card secure area read.\n");
                        return GameCardMisc_ROMReadSecureAreaHandler;
                    }
                    else return GameCardMisc_ROMReadHandler;
                }
                case 0xB8:
                {
                    // chip id
                    return GameCardMisc_UnencIDReadHandler;
                }
                default:
                    return GameCardMisc_InvalidCmdHandler;
            }
            break;
        }
    }
    LogPrint(LOG_CARD|LOG_ODD, "Invalid Game Card cmd %02X %i ran\n", (u8)cmd & 0xFF, card->Mode);
    return GameCardMisc_InvalidCmdHandler;
}

void QueueNextTransfer(Console* sys, timestamp cur, const bool a9)
{
    GameCard* card = &sys->GameCard;

    if (card->NumWords > 0)
    {
        timestamp transtime = 4;
        if(!(card->Address & 0x1FF))
        {
            transtime += sys->GCROMCR[a9].Key1Gap2;
        }

        if (card->Buffered)
        {
            // dont ask me why its only 7. idfk
            transtime += ((sys->GCROMCR[a9].ClockDivider) ? 7 : 5);
        }

        transtime *= ((sys->GCROMCR[a9].ClockDivider) ? 8 : 5);

        Sched_AddEvent(sys, cur+DSClk33(transtime), Evt_CardROM);
    }
    else
    {
        if (!sys->GCROMCR[a9].DataReady)
        {
            sys->GCROMCR[a9].Start = false;
            if (sys->GCSPICR[a9].ROMDataReadyIRQ)
                Sched_AddEvent(sys, cur, a9 ? Evt_IRQ9_NTRCardTranferComplete : Evt_IRQ7_NTRCardTranferComplete); // todo: delay?
        }
    }
}

u32 GameCard_ROMDataRead(Console* sys, timestamp cur, const bool a9)
{
    GameCard* card = &sys->GameCard;

    u32 ret = sys->GCROMData[a9];

    if (card->Buffered)
    {
        QueueNextTransfer(sys, cur, a9);
        sys->GCROMData[a9] = card->WordBuffer;
        card->Buffered = false;
        StartDMA(sys, cur+DSClk33(1), DMAStart_NTRCard, a9); // checkme: delay?
    }
    else
    {
        sys->GCROMCR[a9].DataReady = false;
        QueueNextTransfer(sys, cur, a9);
    }

    return ret;
}

void GameCard_HandleSchedulingROM(Console* sys, timestamp now)
{
    GameCard* card = &sys->GameCard;
    bool a9 = !sys->ExtMemCR_Shared.NDSCardA7Access;

    card->NumWords -= 1;
    u32 data = card->ReadHandler(card);
    if (card->NumWords >= 0) // was not a 0 length command
    {
        if (sys->GCROMCR[a9].DataReady)
        {
            card->WordBuffer = data;
            card->Buffered = true;
        }
        else
        {
            sys->GCROMData[a9] = data;
            sys->GCROMCR[a9].DataReady = true;
            StartDMA(sys, now+DSClk33(1), DMAStart_NTRCard, a9); // checkme: delay?

            QueueNextTransfer(sys, now, a9);
        }
    }
    else QueueNextTransfer(sys, now, a9);
}

void GameCard_ROMCommandSubmit(Console* sys, timestamp cur, const bool a9)
{
    GameCard* card = &sys->GameCard;
    // check if slot is enabled and in ROM mode
    // checkme: should it being in release also prevent rom accesses? one would assume so.
    // TODO: validate all timings. some of them are stolen straight from melonDS, and some are based on some old-ish and low quality research by me.
    if (!sys->GCSPICR[a9].SlotEnable || sys->GCSPICR[a9].CardSPIMode) return;


    card->NumWords = ((sys->GCROMCR[a9].NumWords == 7)
                        ? 1
                        : ((sys->GCROMCR[a9].NumWords == 0)
                            ? 0
                            : (0x40 << sys->GCROMCR[a9].NumWords)));

    card->ReadHandler = GameCardMisc_ROMCommandHandler(sys, a9);

    // calc timings
    timestamp transfertime = 10;
    if (!sys->GCROMCR[a9].Write)
    {
        transfertime += sys->GCROMCR[a9].Key1Gap;
        if (card->NumWords) transfertime += sys->GCROMCR[a9].Key1Gap2+3;
    }
    transfertime *= ((sys->GCROMCR[a9].ClockDivider) ? 8 : 5);
    transfertime += 3;

    Sched_AddEvent(sys, cur+DSClk33(transfertime), Evt_CardROM);
}

void GameCard_SPIFinish(Console* sys, const bool a9)
{
    sys->GCSPIOut[a9] = sys->GCSPIBuf;
    sys->GCSPICR[a9].Busy = false;
}

u32 GameCard_IOReadHandler(Console* sys, u32 addr, const bool a9)
{
    addr -= 0x040001A0;

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardA7Access == a9) return 0; // checkme: all of them && correct ret?

    switch(addr & 0x1C)
    {
        case 0x00:
            return sys->GCSPICR[a9].Raw | (sys->GCSPIOut[a9] << 16);
        case 0x04:
            return sys->GCROMCR[a9].Raw;
        default:
            return 0;
    }
}

void GameCard_IOWriteHandler(Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9)
{
    addr -= 0x040001A0;

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardA7Access == a9) return; // checkme: all of them?

    switch(addr & 0x1C)
    {
        case 0x00:
        {
            MaskedWrite(sys->GCSPICR[a9].Raw, val, mask & 0xE043);
            if (mask & 0xFF0000)
            {
                if (!sys->GCSPICR[a9].SlotEnable)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Game Card SPI writes while slot disabled? Val: %08X Mask: %08X\n", val, mask);
                }
                else if (!sys->GCSPICR[a9].CardSPIMode)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Game Card SPI writes while in ROM mode? Val: %08X Mask: %08X\n", val, mask);
                }
                else if (sys->GCSPICR[a9].Busy)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Game Card SPI writes while busy? Val: %08X Mask: %08X\n", val, mask);
                }
                else
                {
                    sys->GCSPIBuf = GameCardSPI_CmdSend(&sys->GameCard.SPI, sys->GameCard.SPIType, sys->GameCard.SRAMType, (val>>16)&0xFF, sys->GCSPICR[a9].ChipSelect);

                    sys->GCSPICR[a9].Busy = true;
                    Sched_AddEvent(sys, cur + (DSClk33(8*8)<<sys->GCSPICR[a9].Baudrate), (a9 ? Evt_CardSPI9 : Evt_CardSPI7)); // checkme: delay
                }
            }
            break;
        }
        case 0x04:
        {
            MaskedWrite(sys->GCROMCR[a9].Raw, val, mask & 0x5F7F7FFF);

            if (val & mask & (1<<15))
            {
                // TODO: apply key 2
            }

            if (val & mask & (1<<29))
            {
                // write once
                sys->GCROMCR[a9].ReleaseReset = true;
            }

            if (val & mask & (1<<31))
            {
                // only run if enabling
                if (!sys->GCROMCR[a9].Start)
                {
                    // call command handler
                    //sys->GameCard.CommandHandler(sys, sys->GCCommandPort[a9].Raw);
                    GameCard_ROMCommandSubmit(sys, cur, a9);
                }
                // can't be cleared
                sys->GCROMCR[a9].Start = true;
                sys->GCROMCR[a9].DataReady = false;
            }
            break;
        }
        case 0x08:
        {
            MaskedWrite(sys->GCCommandPort[a9].Lo, val, mask);
            if (sys->GCROMCR[a9].Start) LogPrint(LOG_CARD|LOG_UNIMP, "Writing Game Card cmd port while busy?\n");
            break;
        }
        case 0x0C:
        {
            MaskedWrite(sys->GCCommandPort[a9].Hi, val, mask);
            if (sys->GCROMCR[a9].Start) LogPrint(LOG_CARD|LOG_UNIMP, "Writing Game Card cmd port while busy?\n");
            break;
        }
        case 0x10:
        {
            MaskedWrite(sys->GCS2EncrySeeds[0][a9][0].Lo, val, mask);
            break;
        }
        case 0x14:
        {
            MaskedWrite(sys->GCS2EncrySeeds[0][a9][1].Lo, val, mask);
            break;
        }
        case 0x18:
        {
            MaskedWrite(sys->GCS2EncrySeeds[0][a9][0].Hi, val, mask & 0x7F);
            break;
        }
        case 0x1A:
        {
            MaskedWrite(sys->GCS2EncrySeeds[0][a9][1].Hi, val, mask & 0x7F);
            break;
        }
        default:
            break;
    }
}
