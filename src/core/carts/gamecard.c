#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gamecard.h"
#include "../console.h"
#include "../sram/flash.h"
#include "../sram/eeprom.h"



bool Gamecard_Init(Gamecard* card, const char* romname, u8* bios7)
{
    FILE* rom = fopen(romname, "rb");

    if (rom == NULL)
    {
        LogPrint(LOG_ALWAYS, "ERROR: Could not open provided ROM\n");
        return false;
    }

    fseek(rom, 0, SEEK_END);
    u64 size = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    u64 fauxsize = size;
    if (stdc_count_ones(fauxsize) != 1)
    {
        int shift = 0;
        while (stdc_count_ones(fauxsize) != 1)
        {
            fauxsize >>= 1;
            shift++;
        }
        fauxsize <<= shift;
        fauxsize *= 2;
    }

    if (size > MiB(512))
    {
        LogPrint(LOG_ALWAYS, "ERROR: Gamecard ROM too big! must be <= 512MiB; currently %lu\n", size);
        return false;
    }
    if (size < KiB(4))
    {
        LogPrint(LOG_ALWAYS, "ERROR: Gamecard ROM too small! idk if this is actually an issue but it feels like one; currently %lu\n", size);
        return false;
    }

    card->RomSize = fauxsize;
    card->ROM = malloc(fauxsize);
    if (card->ROM == NULL)
    {
        LogPrint(LOG_ALWAYS, "ERROR: Gamecard ROM allocation failed\n");
        return false;
    }

    for (u64 i = size/sizeof(card->ROM[0]); i < fauxsize/sizeof(card->ROM[0]); i++)
    {
        card->ROM[i] = 0;
    }

    if (fread(card->ROM, size, 1, rom) == 0)
    {
        LogPrint(LOG_ALWAYS, "ERROR: Could not read gamecard ROM\n");
        fclose(rom);
        return false;
    }

    fclose(rom);

    memcpy(card->Key1, &bios7[0xC6D0], 1042);


    // BEGIN SOUP PARSING

    card->SPI = nullptr;
    card->ChipID = 0x010101C2;

    char soupname[512]; // if you make a longer file path i *will* cry.

    strncpy(soupname, romname, 512-5); 

    char* end = strrchr(soupname, '.');

    if (end == NULL)
    {
        printf("ERROR: Could not find the part of the file path that's actually a file extension????\n");
        return true;
    }

    end[1] = 's';
    end[2] = 'o';
    end[3] = 'u';
    end[4] = 'p';
    end[5] = '\0';

    FILE* soup = fopen(soupname, "r");

    if (soup == NULL)
    {
        LogPrint(LOG_ALWAYS, "NOTE: Could not locate .soup for this ROM; It probably wont boot properly.\n");
        return true;
    }

    char* soupbowl;

    fseek(soup, 0, SEEK_END);
    u64 soupsize = ftell(soup);
    fseek(soup, 0, SEEK_SET);

    if (soupsize > KiB(1))
    {
        LogPrint(LOG_ALWAYS, "ERROR: Why is this .soup so big?? Max size: 1 KiB. Actual size: %lu\n", soupsize);
        fclose(soup);
        return true;
    }
    if (soupsize == 0)
    {
        LogPrint(LOG_ALWAYS, "ERROR: This .soup is empty???\n");
        fclose(soup);
        return true;
    }

    soupbowl = malloc(soupsize+32); // overallocate to make my life easier

    memset(soupbowl, 0, soupsize+32);

    if (fread(soupbowl, soupsize, 1, soup) == 0)
    {
        LogPrint(LOG_ALWAYS, "ERROR: Could not read .soup\n");
        fclose(soup);
        return true;
    }

    soupbowl[soupsize+31] = '\0'; // im kinda just assuming i have to do this

    char* spoon = strstr(soupbowl, "chipid:");

    if (spoon != NULL)
    {
        char* end;
        card->ChipID = strtoul(&spoon[8], &end, 16);
    }

    int sramsize = 0;
    spoon = strstr(soupbowl, "sramsize:");

    if (spoon != NULL)
    {
        sramsize = atoi(&spoon[9]);
    }

    if (sramsize > 0 && sramsize <= 24)
    {
        spoon = strstr(soupbowl, "spi:");

        u64 search = (spoon[4] == ' ') ? 5 : 4; // make spaces optional ig

        if (memcmp(&spoon[search], "flash", 5) == 0)
        {
            printf("FLASH\n");
            card->SPI = malloc(sizeof(Flash));
            if (card->SPI == NULL)
            {
                LogPrint(LOG_ALWAYS, "Could not allocate RAM for gamecard SPI\n");
                card->SPI = nullptr;
                fclose(soup);
                return true;
            }

            spoon = strstr(soupbowl, "flashid:");

            u32 id = 0x010101;

            if (spoon != NULL)
            {
                char* end;
                id = strtoul(&spoon[8], &end, 16);
            }

            if (!Flash_Init(card->SPI, NULL /* TODO */, 1<<sramsize, false, id, "SRAM Flash"))
            {
                Flash_Cleanup(card->SPI);

                card->SPI = nullptr;
            }
            else
            {
                card->SPI_CMDSend = (void*)Flash_CMDSend;
                card->SPI_Cleanup = (void*)Flash_Cleanup;
            }
        }
        else if (memcmp(&spoon[search], "eep9", 4) == 0)
        {
            printf("EEP9\n");
            card->SPI = malloc(sizeof(EEPROM));
            if (card->SPI == NULL)
            {
                LogPrint(LOG_ALWAYS, "Could not allocate RAM for gamecard SPI\n");
                card->SPI = nullptr;
                fclose(soup);
                return true;
            }

            if (!EEPROM_Init(card->SPI, NULL /* TODO */, 1<<sramsize, 1, 0 /* TODO */))
            {
                EEPROM_Cleanup(card->SPI);

                card->SPI = nullptr;
            }
            else
            {
                card->SPI_CMDSend = (void*)EEPROM_CMDSend;
                card->SPI_Cleanup = (void*)EEPROM_Cleanup;
            }
        }
        else if (memcmp(&spoon[search], "eep16", 5) == 0)
        {
            card->SPI = malloc(sizeof(EEPROM));
            if (card->SPI == NULL)
            {
                LogPrint(LOG_ALWAYS, "Could not allocate RAM for gamecard SPI\n");
                card->SPI = nullptr;
                fclose(soup);
                return true;
            }

            if (!EEPROM_Init(card->SPI, NULL /* TODO */, 1<<sramsize, 2, 0 /* TODO */))
            {
                EEPROM_Cleanup(card->SPI);

                card->SPI = nullptr;
            }
            else
            {
                card->SPI_CMDSend = (void*)EEPROM_CMDSend;
                card->SPI_Cleanup = (void*)EEPROM_Cleanup;
            }
        }
        else if (memcmp(&spoon[search], "eep24", 5) == 0)
        {
            card->SPI = malloc(sizeof(EEPROM));
            if (card->SPI == NULL)
            {
                LogPrint(LOG_ALWAYS, "Could not allocate RAM for gamecard SPI\n");
                card->SPI = nullptr;
                fclose(soup);
                return true;
            }

            if (!EEPROM_Init(card->SPI, NULL /* TODO */, 1<<sramsize, 3, 0 /* TODO */))
            {
                EEPROM_Cleanup(card->SPI);

                card->SPI = nullptr;
            }
            else
            {
                card->SPI_CMDSend = (void*)EEPROM_CMDSend;
                card->SPI_Cleanup = (void*)EEPROM_Cleanup;
            }
        }
    }
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
    u32 code[3] = {card->ROM[0xC], card->ROM[0xC]>>1 ,card->ROM[0xC]<<1};
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
    u32 ret = card->ROM[(0x1000+card->Address)/sizeof(u32)];
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
            Key1_Decrypt(card, (u32*)&cmd); // type punning...
            cmd = bswap(cmd);

            switch(cmd & 0xF0)
            {
                case 0x40:
                    return GamecardMisc_InvalidCmdHandler; // idk?
                case 0x10:
                    return GamecardMisc_UnencIDReadHandler;
                case 0x20:
                    card->Address = ((cmd >> 24) & 0xF0) << 8;
                    return GamecardMisc_ReadSecureAreaHandler;
                case 0xA0:
                    card->Mode = Key2;
                    return GamecardMisc_InvalidCmdHandler; // idk?
            }
            break;
        }
        case Key2:
        {
            switch(cmd & 0xFF)
            {
                case 0xB7:
                {
                    // data
                    card->Address = (bswap(cmd) >> 24) & (card->RomSize-4); // subtract 4 as a weird way to handle masking out bottom bits as well.
                    if (!(card->Address & ~(KiB(4)-1)))
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
    LogPrint(LOG_CARD|LOG_ODD, "invalid gamecard cmd %02X ran\n", (u8)cmd & 0xFF);
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
        sys->GCROMCR[a9].Start = false;
        if (sys->GCSPICR[a9].ROMDataReadyIRQ) Console_ScheduleIRQs(sys, IRQ_GamecardTransferComplete, a9, cur); // todo: delay?
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_CardROM, timestamp_max);
    }
}

u32 Gamecard_ROMDataRead(struct Console* sys, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9);
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
        //QueueNextTransfer(sys, cur, a9);
    }

    return ret;
}

void Gamecard_HandleSchedulingROM(struct Console* sys, timestamp now)
{
    Gamecard* card = &sys->Gamecard;
    bool a9 =!sys->ExtMemCR_Shared.NDSCardAccess;

    card->NumWords -= 1;
    if (sys->GCROMCR[a9].DataReady)
    {
        card->Buffered = true;
        card->WordBuffer = card->ReadHandler(card);
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_CardROM, timestamp_max);
    }
    else
    {
        sys->GCROMData[a9] = card->ReadHandler(card);
        sys->GCROMCR[a9].DataReady = true;
        if (a9) StartDMA9(sys, now+1, DMAStart_NTRCard); // checkme: delay?
        else StartDMA7(sys, now+1, DMAStart_NTRCard); // checkme: delay?

        QueueNextTransfer(sys, now, a9);
    }
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
    sys->GCSPIOut[true] = sys->SPIBuf;
    sys->GCSPICR[true].Busy = false;
    Schedule_Event(sys, NULL, Evt_CardSPI, timestamp_max);
}

void Gamecard_SPIFinish7(struct Console* sys, [[maybe_unused]] timestamp cur)
{
    sys->GCSPIOut[false] = sys->SPIBuf;
    sys->GCSPICR[false].Busy = false;
    Schedule_Event(sys, NULL, Evt_CardSPI, timestamp_max);
}

u32 Gamecard_IOReadHandler(struct Console* sys, u32 addr, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9);
    addr -= 0x040001A0;

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardAccess == a9) return 0; // checkme: all of them && correct ret?

    switch(addr & 0x1C)
    {
        case 0x00:
            Scheduler_RunEventManual(sys, cur, Evt_CardSPI, a9);
            return sys->GCSPICR[a9].Raw | (sys->GCSPIOut[a9] << 16);
        case 0x04:
            Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9);
            return sys->GCROMCR[a9].Raw;
        default:
            return 0;
    }
}

void Gamecard_IOWriteHandler(struct Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9)
{
    addr -= 0x040001A0;
    Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9);

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardAccess == a9) return; // checkme: all of them?

    switch(addr & 0x1C)
    {
        case 0x00:
        {
            MaskedWrite(sys->GCSPICR[a9].Raw, val, mask & 0xE043);
            if (mask & 0xFF0000)
            {
                if (sys->GCSPICR[a9].Busy)
                {
                    LogPrint(LOG_CARD|LOG_ODD, "Gamecard SPI writes while busy?\n");
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
            Scheduler_RunEventManual(sys, cur, Evt_CardROM, a9);
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
