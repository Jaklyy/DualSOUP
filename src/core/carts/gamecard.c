#include "gamecard.h"
#include "../console.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>




// todo SRAM
bool Gamecard_Init(Gamecard* card, FILE* rom, u8* bios7)
{
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
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM too big! must be <= 512MiB; currently %li\n", size);
        return false;
    }
    if (size < KiB(4))
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM too small! idk if this is actually an issue but it feels like one; currently %li\n", size);
        return false;
    }

    card->RomSize = fauxsize;
    card->ROM = malloc(fauxsize);
    if (card->ROM == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM allocation failed\n");
        return false;
    }

    for (u64 i = size/sizeof(card->ROM[0]); i < fauxsize/sizeof(card->ROM[0]); i++)
    {
        card->ROM[i] = 0;
    }

    if (fread(card->ROM, size, 1, rom) == 0)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM read failed\n");
        return false;
    }

    memcpy(card->Key1, &bios7[0xC6D0], 1042);

    if (!Flash_InitB(&card->SRAM, KiB(256))) return false;
    // TODO: make configurable
    card->ChipID = 0x010101C2;
    return true;
}

void Gamecard_Cleanup(Gamecard* card)
{
    free(card->ROM);
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

        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_Gamecard, cur+transtime);
    }
    else
    {
        sys->GCROMCR[a9].Start = false;
        if (sys->GCSPICR[a9].ROMDataReadyIRQ) Console_ScheduleIRQs(sys, IRQ_GamecardTransferComplete, a9, cur); // todo: delay?
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_Gamecard, timestamp_max);
    }
}

u32 Gamecard_ROMDataRead(struct Console* sys, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Evt_Gamecard, a9);
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
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_Gamecard, timestamp_max);
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

    Schedule_Event(sys, Gamecard_HandleSchedulingROM, Evt_Gamecard, cur+transfertime);
}

u32 Gamecard_IOReadHandler(struct Console* sys, u32 addr, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Evt_Gamecard, a9);
    addr -= 0x040001A0;

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardAccess == a9) return 0; // checkme: all of them && correct ret?

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

void Gamecard_IOWriteHandler(struct Console* sys, u32 addr, const u32 val, const u32 mask, timestamp cur, const bool a9)
{
    addr -= 0x040001A0;
    Scheduler_RunEventManual(sys, cur, Evt_Gamecard, a9);

    // a7 is set for exmemcnt bits
    if (sys->ExtMemCR_Shared.NDSCardAccess == a9) return; // checkme: all of them?

    switch(addr & 0x1C)
    {
        case 0x00:
        {
            MaskedWrite(sys->GCSPICR[a9].Raw, val, mask);
            if (mask & 0xFF0000)
            {
                // TODO: SPI Comms
                MaskedWrite(sys->GCSPICR[a9].Raw, val, mask & 0xE043);
                sys->GCSPIOut[a9] = Flash_CMDSend(&sys->Gamecard.SRAM, (val>>16)&0xFF, sys->GCSPICR[a9].ChipSelect);
                //LogPrint(LOG_UNIMP|LOG_CARD, "SPI COMMS REQUESTED\n");
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
