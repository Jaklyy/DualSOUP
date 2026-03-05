#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gamecard.h"
#include "../console.h"
#include "../sram/flash.h"
#include "../sram/eeprom.h"
#include "../../frontend/soupparser/soupparser.h"



bool Gamecard_Init(Gamecard* card, const char* romname, u8* bios7)
{
    FILE* rom;
    if ((rom = fopen(romname, "rb")) == NULL)
    {
        perror("ERROR: Could not open provided ROM");
        return false;
    }

    if (fseek(rom, 0, SEEK_END))
    {
        perror("Seek Error");
        fclose(rom);
        return false;
    }

    u64 filesize = ftell(rom);
    u64 chipsize = (u64)0x8000'0000'0000'0000 >> (stdc_leading_zeros(filesize) - (stdc_count_ones(filesize) != 1));

    if (chipsize > GiB(2))
    {
        LogPrint(LOG_ALWAYS, "ERROR: Gamecard ROM too big! Must be <= 2GiB! size: %lu\n", chipsize);
        fclose(rom);
        return false;
    }
    if (chipsize < KiB(4))
    {
        LogPrint(LOG_ALWAYS, "NOTE: Gamecard ROM too small! padding to 4KiB size: %lu\n", chipsize);
        chipsize = KiB(4);
    }

    if (fseek(rom, 0, SEEK_SET))
    {
        perror("Seek Error");
        fclose(rom);
        return false;
    }

    card->RomSize = chipsize;
    if ((card->ROM = malloc(chipsize)) == NULL)
    {
        LogPrint(LOG_ALWAYS, "ERROR: Could not allocate memory for Gamecard ROM.\n");
        fclose(rom);
        return false;
    }

    if (filesize != chipsize)
    {
        LogPrint(LOG_ALWAYS, "NOTE: filesize not power of 2, trimmed ROM? Padding with FF. file: %lu chip: %lu\n", filesize, chipsize);
        memset((void*)((intptr_t)(&card->ROM)+filesize), 0xFF, chipsize-filesize);
    }

    if (fread(card->ROM, filesize, 1, rom) == 0)
    {
        perror("ERROR: Could not read gamecard ROM");
        fclose(rom);
        return false;
    }
    fclose(rom);

    memcpy(card->Key1, &bios7[0x30], sizeof(card->Key1));


    // BEGIN SOUP PARSING

    // load defaults
    card->SPI = nullptr;
    card->ChipID = DefaultChipID;

    FILE* soup;
    if ((soup = FindFileWithSameName(romname, "soup", "r")) == NULL)
    {
        LogPrint(LOG_ALWAYS, "NOTE: Could not locate .soup for this ROM; SPI/SRAM will not be configured. Things may break.\n");

        return true;
    }

    char* soupbowl;

    if (fseek(soup, 0, SEEK_END))
    {
        perror(".soup Seek Error");
        fclose(soup);
        return true;
    }
    u64 soupsize = ftell(soup);
    if (fseek(soup, 0, SEEK_SET))
    {
        perror(".soup Seek Error");
        fclose(soup);
        return true;
    }

    soupbowl = malloc(soupsize+32); // overallocate to make my life easier

    memset(soupbowl, 0, soupsize+32);

    if (fread(soupbowl, soupsize, 1, soup) == 0)
    {
        perror("ERROR: Could not read .soup\n");
        fclose(soup);
        return true;
    }
    fclose(soup);

    soupbowl[soupsize+31] = '\0'; // im kinda just assuming i have to do this

    if (SOUPParser(soupbowl, "chipid:", NULL, SEARCH_U32HEX, &card->ChipID))
    {
        LogPrint(LOG_ALWAYS, "ChipID found: %08X\n", card->ChipID);
    }
    else
    {
        LogPrint(LOG_ALWAYS, "No chipid specified in .soup, loading default: %08X\n", DefaultChipID);
    }

    u64 sramsize;
    if (SOUPParser(soupbowl, "sramsize:", NULL, SEARCH_U64DEC, &sramsize))
    {
        if ((sramsize > 24) || (sramsize < 9))
        {
            LogPrint(LOG_ALWAYS, "SRAMSize found, but invalid size: max 24, min 9: val:%li size:%liB; SPI/SRAM will not be configured. Things may break.\n", sramsize, (u64)1<<sramsize);
            return true;
        }

        LogPrint(LOG_ALWAYS, "SRAMSize found: val:%li size:%liB\n", sramsize, (u64)1<<sramsize);
    }
    else
    {
        LogPrint(LOG_ALWAYS, "ERROR: sramsize unspecified in .soup; SPI/SRAM will not be configured. Things may break.\n");
        return true;
    }
    sramsize = 1<<sramsize;

    u8* sram = malloc(sramsize);
    FILE* sav;
    if ((sav = FindFileWithSameName(romname, "sav", "rb")) == NULL)
    {
        LogPrint(LOG_ALWAYS, "NOTE: Could not locate .sav for this ROM. Creating uninitialized save ram.\n");
        memset(sram, 0xFF, sramsize); // CHECKME
    }
    else
    {
        if (fseek(sav, 0, SEEK_END))
        {
            perror("Seek Error");
            return false;
        }
        u64 savsize = ftell(sav);
        if (fseek(sav, 0, SEEK_SET))
        {
            perror("Seek Error");
            return false;
        }
        if (fread(sram, savsize, 1, sav) == 0)
        {
            perror("ERROR: Could not read .sav\n");
            fclose(sav);
            return false;
        }
        fclose(sav);
        memset(&sram[savsize], 0xFF, sramsize - savsize);
    }

    u8 addrbytes;
    if (SOUPParser(soupbowl, "spi:", "flash", SEARCH_STRING, NULL))
    {
        card->SPI = malloc(sizeof(Flash));
        if (card->SPI == NULL)
        {
            LogPrint(LOG_ALWAYS, "Could not allocate RAM for gamecard flash\n");
            return false;
        }
        memset(card->SPI, 0, sizeof(Flash));

        u32 flashid;
        if (!SOUPParser(soupbowl, "flashid:", NULL, SEARCH_U32HEX, &flashid))
        {
            flashid = 0x00010203;
        }

        Flash_Init(card->SPI, sram, sramsize, false, flashid);
        card->SPI_CMDSend = (void*)Flash_CMDSend;
        card->SPI_Cleanup = (void*)Flash_Cleanup;
        return true;
    }
    else if (SOUPParser(soupbowl, "spi:", "eep9", SEARCH_STRING, NULL))
    {
        addrbytes = 1;
    }
    else if (SOUPParser(soupbowl, "spi:", "eep16", SEARCH_STRING, NULL))
    {
        addrbytes = 2;
    }
    else if (SOUPParser(soupbowl, "spi:", "eep24", SEARCH_STRING, NULL))
    {
        addrbytes = 3;
    }
    else
    {
        LogPrint(LOG_ALWAYS, "ERROR: UNK SRAM TYPE SPECIFIED?\n");
        return false;
    }

    card->SPI = malloc(sizeof(EEPROM));
    if (card->SPI == NULL)
    {
        LogPrint(LOG_ALWAYS, "Could not allocate RAM for gamecard eeprom\n");
        return false;
    }

    memset(card->SPI, 0, sizeof(EEPROM));
    EEPROM_Init(card->SPI, sram, sramsize, addrbytes, 0);
    card->SPI_CMDSend = (void*)EEPROM_CMDSend;
    card->SPI_Cleanup = (void*)EEPROM_Cleanup;
    return true;
}

void Gamecard_Cleanup(Gamecard* card)
{
    if (card->SPI != nullptr)
    {
        card->SPI_Cleanup((card->SPI));
        free(card->SPI);
        card->SPI = nullptr;
    }
    free(card->ROM);
    card->ROM = nullptr;
}

// TODO: reset func?

// this is just ripped from melonds
void Key1_Encrypt(Gamecard* card, u32* data)
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
void Key1_Decrypt(Gamecard* card, u32* data)
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
void Key1_Apply(Gamecard* card, u32* code, u32 mod)
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
void GamecardMisc_InitKey1(Gamecard* card)
{
    u32 code[3] = {card->ROM[0xC/sizeof(u32)], card->ROM[0xC/sizeof(u32)]>>1 ,card->ROM[0xC/sizeof(u32)]<<1};
    Key1_Apply(card, code, 2);
    Key1_Apply(card, code, 2);
    card->Mode = Key1;
}

u32 GamecardMisc_ROMReadHandler(Gamecard* card)
{
    u32 ret = card->ROM[card->Address/sizeof(u32)];
    card->Address += 4;
    // force it to stay within one 4 KiB area.
    if (!(card->Address & (KiB(4)-1)))
    {
        if (card->NumWords) LogPrint(LOG_CARD, "Gamecard read wrapping.\n");
        card->Address -= KiB(4);
    }

    return ret;
}

u32 GamecardMisc_ROMReadSecureAreaHandler(Gamecard* card)
{
    u32 ret = card->ROM[(0x8000+card->Address)/sizeof(u32)];
    card->Address += 4;
    card->Address &= 0x1FF;

    return ret;
}

u32 GamecardMisc_ReadSecureAreaHandler(Gamecard* card)
{
    // TODO: what does this actually do???
    u32 ret = card->ROM[card->Address/sizeof(u32)];
    card->Address += 4;
    return ret;
}

u32 GamecardMisc_UnencIDReadHandler(Gamecard* card)
{
    return card->ChipID;
}

u32 GamecardMisc_UnencHeaderHandler(Gamecard* card)
{
    card->Address &= 0xFFF;
    u32 ret = card->ROM[card->Address/sizeof(u32)];
    card->Address += 4;
    return ret;
}

u32 GamecardMisc_InvalidCmdHandler([[maybe_unused]] Gamecard* card)
{
    return 0xFFFFFFFF; // idk
}

void* GamecardMisc_ROMCommandHandler(struct Console* sys, const bool a9)
{
    Gamecard* card = &sys->Gamecard;
    u64 cmd = sys->GCCommandPort[a9].Raw;
    switch(card->Mode)
    {
        case Unenc:
        {
            switch(cmd & 0xFF)
            {
                case 0x9F:
                    // High Z
                    return GamecardMisc_InvalidCmdHandler;
                case 0x00:
                    // header
                    card->Address = (bswap(cmd) >> 24) & ~3;
                    return GamecardMisc_UnencHeaderHandler;
                case 0x90:
                    // chip id
                    return GamecardMisc_UnencIDReadHandler;
                case 0x3C:
                    // active key 1
                    GamecardMisc_InitKey1(card);
                    return GamecardMisc_InvalidCmdHandler; // idk?
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
                    return GamecardMisc_InvalidCmdHandler; // idk?
                case 0x10:
                    return GamecardMisc_UnencIDReadHandler;
                case 0x20:
                    card->Address = ((bswap(cmd) >> 44) & 0x7) << 12; // checkme: decoding on this seems weird; are the nibbles swapped?
                    return GamecardMisc_ReadSecureAreaHandler;
                case 0xA0:
                    card->Mode = Key2;
                    return GamecardMisc_InvalidCmdHandler; // idk?
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
                    if (card->Address >= card->RomSize) LogPrint(LOG_CARD, "Gamecard address space wrapping: %08X %08X\n", card->Address, card->RomSize);
                    card->Address &= (card->RomSize-4); // subtract 4 as a weird way to handle masking out bottom bits as well.
                    if (card->Address < 0x8000)
                    {
                        // secure area is rerouted to the 512 bytes above it
                        card->Address &= 0x1FF;
                        LogPrint(LOG_CARD, "Gamecard secure area read.\n");
                        return GamecardMisc_ROMReadSecureAreaHandler;
                    }
                    else return GamecardMisc_ROMReadHandler;
                }
                case 0xB8:
                {
                    // chip id
                    return GamecardMisc_UnencIDReadHandler;
                }
                default:
                    return GamecardMisc_InvalidCmdHandler;
            }
            break;
        }
    }
    LogPrint(LOG_CARD|LOG_ODD, "Invalid Gamecard cmd %02X %i ran\n", (u8)cmd & 0xFF, card->Mode);
    return GamecardMisc_InvalidCmdHandler;
}

void Gamecard_HandleSchedulingROM(struct Console* sys, timestamp now);

void QueueNextTransfer(struct Console* sys, timestamp cur, const bool a9)
{
    Gamecard* card = &sys->Gamecard;

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

        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_CardROM, cur+transtime);
    }
    else
    {
        if (!sys->GCROMCR[a9].DataReady)
        {
            sys->GCROMCR[a9].Start = false;
            if (sys->GCSPICR[a9].ROMDataReadyIRQ)
                Console_ScheduleIRQs(sys, IRQ_GamecardTransferComplete, a9, cur); // todo: delay?
        }
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_CardROM, timestamp_max);
    }
}

u32 Gamecard_ROMDataRead(struct Console* sys, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9, true);
    Gamecard* card = &sys->Gamecard;

    u32 ret = sys->GCROMData[a9];

    if (card->Buffered)
    {
        QueueNextTransfer(sys, cur, a9);
        sys->GCROMData[a9] = card->WordBuffer;
        card->Buffered = false;
        if (a9) StartDMA9(sys, cur+1, DMAStart_NTRCard); // checkme: delay?
        else StartDMA7(sys, cur+1, DMAStart_NTRCard); // checkme: delay?
    }
    else
    {
        sys->GCROMCR[a9].DataReady = false;
        QueueNextTransfer(sys, cur, a9);
    }

    return ret;
}

void Gamecard_HandleSchedulingROM(struct Console* sys, timestamp now)
{
    Gamecard* card = &sys->Gamecard;
    bool a9 = !sys->ExtMemCR_Shared.NDSCardAccess;

    card->NumWords -= 1;
    u32 data = card->ReadHandler(card);
    if (card->NumWords >= 0) // was not a 0 length command
    {
        if (sys->GCROMCR[a9].DataReady)
        {
            card->WordBuffer = data;
            card->Buffered = true;
            Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_CardROM, timestamp_max);
        }
        else
        {
            sys->GCROMData[a9] = data;
            sys->GCROMCR[a9].DataReady = true;
            if (a9) StartDMA9(sys, now+1, DMAStart_NTRCard); // checkme: delay?
            else StartDMA7(sys, now+1, DMAStart_NTRCard); // checkme: delay?

            QueueNextTransfer(sys, now, a9);
        }
    }
    else QueueNextTransfer(sys, now, a9);
}

void Gamecard_ROMCommandSubmit(struct Console* sys, timestamp cur, const bool a9)
{
    Gamecard* card = &sys->Gamecard;
    // check if slot is enabled and in ROM mode
    // checkme: should it being in release also prevent rom accesses? one would assume so.
    // TODO: validate all timings. some of them are stolen straight from melonDS, and some are based on some old-ish and low quality research by me.
    if (!sys->GCSPICR[a9].SlotEnable || sys->GCSPICR[a9].CardSPIMode) return;


    card->NumWords = ((sys->GCROMCR[a9].NumWords == 7)
                        ? 1
                        : ((sys->GCROMCR[a9].NumWords == 0)
                            ? 0
                            : (0x40 << sys->GCROMCR[a9].NumWords)));

    card->ReadHandler = GamecardMisc_ROMCommandHandler(sys, a9);

    // calc timings
    timestamp transfertime = 10;
    if (!sys->GCROMCR[a9].Write)
    {
        transfertime += sys->GCROMCR[a9].Key1Gap;
        if (card->NumWords) transfertime += sys->GCROMCR[a9].Key1Gap2+3;
    }
    transfertime *= ((sys->GCROMCR[a9].ClockDivider) ? 8 : 5);
    transfertime += 3;

    Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_CardROM, cur+transfertime);
}

void Gamecard_SPIFinish9(struct Console* sys, [[maybe_unused]] timestamp cur)
{
    sys->GCSPIOut[true] = sys->GCSPIBuf;
    sys->GCSPICR[true].Busy = false;
    Schedule_Event(sys, nullptr, Evt_CardSPI, timestamp_max);
}

void Gamecard_SPIFinish7(struct Console* sys, [[maybe_unused]] timestamp cur)
{
    sys->GCSPIOut[false] = sys->GCSPIBuf;
    sys->GCSPICR[false].Busy = false;
    Schedule_Event(sys, nullptr, Evt_CardSPI, timestamp_max);
}

u32 Gamecard_IOReadHandler(struct Console* sys, u32 addr, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9, true);
    addr -= 0x040001A0;

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardAccess == a9) return 0; // checkme: all of them && correct ret?

    switch(addr & 0x1C)
    {
        case 0x00:
            Scheduler_RunEventManual(sys, cur, Evt_CardSPI, a9, true);
            return sys->GCSPICR[a9].Raw | (sys->GCSPIOut[a9] << 16);
        case 0x04:
            Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9, true);
            return sys->GCROMCR[a9].Raw;
        default:
            return 0;
    }
}

void Gamecard_IOWriteHandler(struct Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9)
{
    addr -= 0x040001A0;
    Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9, true);

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardAccess == a9) return; // checkme: all of them?

    switch(addr & 0x1C)
    {
        case 0x00:
        {
            MaskedWrite(sys->GCSPICR[a9].Raw, val, mask & 0xE043);
            if (mask & 0xFF0000)
            {
                if (!sys->GCSPICR[a9].SlotEnable)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Gamecard SPI writes while slot disabled? Val: %08X Mask: %08X\n", val, mask);
                }
                else if (!sys->GCSPICR[a9].CardSPIMode)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Gamecard SPI writes while in ROM mode? Val: %08X Mask: %08X\n", val, mask);
                }
                else if (sys->GCSPICR[a9].Busy)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Gamecard SPI writes while busy? Val: %08X Mask: %08X\n", val, mask);
                }
                else
                {
                    if (sys->Gamecard.SPI == nullptr) sys->GCSPIBuf = 0xFF;
                    else sys->GCSPIBuf = sys->Gamecard.SPI_CMDSend(sys->Gamecard.SPI, (val>>16)&0xFF, sys->GCSPICR[a9].ChipSelect);

                    sys->GCSPICR[a9].Busy = true;
                    Schedule_Event(sys, (a9 ? Gamecard_SPIFinish9 : Gamecard_SPIFinish7), Evt_CardSPI, cur + (8*(8<<sys->GCSPICR[a9].Baudrate))); // checkme: delay
                }
            }
            break;
        }
        case 0x04:
        {
            Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9, true);
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
                    //sys->Gamecard.CommandHandler(sys, sys->GCCommandPort[a9].Raw);
                    Gamecard_ROMCommandSubmit(sys, cur, a9);
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
            if (sys->GCROMCR[a9].Start) LogPrint(LOG_CARD|LOG_UNIMP, "Writing gamecard cmd port while busy?\n");
            break;
        }
        case 0x0C:
        {
            MaskedWrite(sys->GCCommandPort[a9].Hi, val, mask);
            if (sys->GCROMCR[a9].Start) LogPrint(LOG_CARD|LOG_UNIMP, "Writing gamecard cmd port while busy?\n");
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
