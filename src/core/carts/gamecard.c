#include "gamecard.h"
#include "../console.h"
#include <stdlib.h>
#include <stdio.h>




// todo SRAM
bool Gamecard_Init(Gamecard* card, FILE* rom)
{
    fseek(rom, 0, SEEK_END);
    u64 size = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    if (stdc_count_ones(size) != 1)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM size %li is not a power of 2!\n", size);
        return false;
    }
    if (size > MiB(512))
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM too big! must be <= 512MiB; currently %li\n", size);
        return false;
    }
    card->RomSize = size;
    card->ROM = malloc(size);
    if (card->ROM == NULL)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM allocation failed\n");
        return false;
    }

    if (fread(card->ROM, card->RomSize, 1, rom) == 0)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Gamecard ROM read failed\n");
        return false;
    }

    if (!Flash_InitB(&card->SRAM, KiB(256))) return false;
    // TODO: make configurable
    card->ChipID = 0x01010101;
    return true;
}

void Gamecard_Cleanup(Gamecard* card)
{
    free(card->ROM);
}

// TODO: reset func?

u32 GamecardMisc_ROMReadB7Handler(Gamecard* card)
{
    u32 ret = card->ROM[card->Address/sizeof(u32)];
    card->Address += 4;
    // force it to stay within one 4 KiB area.
    if (!(card->Address & (KiB(4)-1)))
    {
        card->Address -= KiB(4);
    }

    return ret;
}

u32 GamecardMisc_ROMReadB7SecureAreaHandler(Gamecard* card)
{
    u32 ret = card->ROM[(0x1000+card->Address)/sizeof(u32)];
    card->Address += 4;
    card->Address &= 0x1FF;

    return ret;
}

u32 GamecardMisc_IDReadB8Handler(Gamecard* card)
{
    return card->ChipID;
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
                    break;
                case 0x00:
                    // header
                    break;
                case 0x90:
                    // chip id
                    break;
                case 0x3C:
                    // active key 1
                    break;
            }
            break;
        }
        case Key1:
            break;
        case Key2:
        {
            switch(cmd & 0xFF)
            {
                case 0xB7:
                {
                    // data
                    card->Address = ((((cmd >> 8) & 0xFF) << 24) | (((cmd >> 16) & 0xFF) << 16) | (((cmd >> 24) & 0xFF) << 8) | (((cmd >> 32) & 0xFF) << 0)) & (card->RomSize-4); // subtract 4 as a weird way to handle masking out bottom bits as well.
                    if (!(card->Address & ~(KiB(4)-1)))
                    {
                        // secure area is rerouted to the 512 bytes above the 
                        card->Address &= 0x1FF;
                        return GamecardMisc_ROMReadB7SecureAreaHandler;
                    }
                    else return GamecardMisc_ROMReadB7Handler;
                }
                case 0xB8:
                {
                    // chip id
                    return GamecardMisc_IDReadB8Handler;
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

    if (card->NumWords)
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

        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Sched_Gamecard, cur+transtime);
    }
    else
    {
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Sched_Gamecard, timestamp_max);
    }
}

u32 Gamecard_ROMDataRead(struct Console* sys, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Sched_Gamecard, a9);
    Gamecard* card = &sys->Gamecard;

    u32 ret = sys->GCROMData[a9];

    if (card->Buffered)
    {
        QueueNextTransfer(sys, cur, a9);
        sys->GCROMData[a9] = card->WordBuffer;
        card->Buffered = false;
    }
    else sys->GCROMCR[a9].DataReady = false;

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
        Schedule_Event(sys, Gamecard_HandleSchedulingROM, Sched_Gamecard, timestamp_max);
    }
    else
        sys->GCROMData[a9] = card->ReadHandler(card);

    QueueNextTransfer(sys, now, a9);
}

void Gamecard_ROMCommandSubmit(struct Console* sys, timestamp cur, const bool a9)
{
    Gamecard* card = &sys->Gamecard;
    // check if slot is enabled and in ROM mode
    // checkme: should it being in release also prevent rom accesses? one would assume so.
    // TODO: validate all timings. some of them are stolen straight from melonDS, and some are based on some old-ish and low quality research by me.
    if (!sys->GCSPICR[a9].SlotEnable || sys->GCSPICR[a9].CardSPIMode) return;


    card->NumWords = ((sys->GCROMCR[a9].NumWords == 7)
                        ? 4
                        : ((sys->GCROMCR[a9].NumWords == 0)
                            ? 0
                            : (0x100 << sys->GCROMCR[a9].NumWords)));

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

    Schedule_Event(sys, Gamecard_HandleSchedulingROM, Sched_Gamecard, cur+transfertime);
}

u32 Gamecard_IOReadHandler(struct Console* sys, u32 addr, timestamp cur, const bool a9)
{
    Scheduler_RunEventManual(sys, cur, Sched_Gamecard, a9);
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
    Scheduler_RunEventManual(sys, cur, Sched_Gamecard, a9);

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
