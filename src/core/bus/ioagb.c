#include "core/console.h"
#include "core/video/video.h"


void IOAGB_Read(Console* sys, const u32 addr, const timestamp now)
{
    u32 rdata = sys->Bus7.ReadBus;
    switch(addr & 0xFF'FF'FC)
    {
    // video A
    case 0x00'00'00: // dispcnt
    case 0x00'00'08 ... 0x00'00'54: // ppu/lcd block
        rdata = PPU_IORead(&sys->PPU_A, addr, true);  break;

    case 0x00'00'04: rdata = (sys->VCountAGB << 16) | sys->DispStatRO7.Raw | sys->DispStatRW7.Raw; break;

    case 0x00'00'88: rdata = (sys->SoundBias & 0x3FE) | sys->SoundRate<<14; break;

    case 0x00'00'B8:
    case 0x00'00'C4:
    case 0x00'00'D0:
    case 0x00'00'DC: rdata = (sys->DMA7[DMA7_NormalBase].CR.Raw & 0xFFFF0000); break;

    case 0x00'01'00 ... 0x00'01'0C: rdata = Timer_IOReadHandler(sys, now, addr, false); break;

    case 0x00'01'30:
        if (Console_TestIfPollingNeeded(sys, now))
        {
            Sched_AddEvent(sys, now, Evt_PollInput);
            Sched_AddEvent(sys, now, Evt_IO7); // resolves last due to scheduler priority
            return;
        }
        else rdata = sys->InputMain;
        break;

    case 0x00'01'34: rdata = sys->RCR; break;

    case 0x00'02'00: rdata = sys->IE7 | (sys->IF7 << 16); break;

    case 0x00'02'04: rdata = sys->ExtMemCR_AGB.Raw; break;

    case 0x00'02'08: rdata = sys->IME7; break;


    case 0x00'03'00: rdata = sys->PostFlag; break;

    default:
        LogPrint(LOG_ARM7 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IOAGB READ: %08"PRIX32" @ %08"PRIX32"\n", addr, sys->A7TDMI.ARM.PC);
        break;
    }
    LogPrint(LOG_IOALL|LOG_ARM7, "IOAGB READ: ADDR:%08X RDATA:%08X\n", addr, rdata);

    Bus_TransferPostSetup(sys, rdata, true, now, false, false);
}

void IOAGB_Write(Console* sys, const u32 addr, timestamp now, const u32 wrdata, const u32 mask)
{
    switch(addr & 0xFF'FF'FC)
    {
    case 0x00'00'00:
    case 0x00'00'08 ... 0x00'00'54:
    case 0x00'00'6C:
        PPU_Sync(sys, now);
        PPU_IOWrite(&sys->PPU_A, addr, wrdata, mask, false, true, true);
        break;

    case 0x00'00'04:
        MaskedWrite(sys->DispStatRW7.Raw, wrdata, mask & 0xFF38);
        sys->TargetVCount7 = sys->DispStatRW7.VCountLSB;
        break;

    case 0x00'00'88:
    {
        MaskedWrite(sys->SoundBias, wrdata, mask & 0x3FE);
        MaskedWrite(sys->SoundRate, wrdata>>14, (mask >> 14) & 0x3);
        break;
    }

    case 0x00'00'B0 ... 0x00'00'DC: DMA7_IOWriteHandler(sys, now, &sys->DMA7[DMA7_NormalBase], addr, wrdata, mask); break;

    case 0x00'01'00 ... 0x00'01'0C: Timer_IOWriteHandler(sys, now, addr, wrdata, mask, false); break;

    case 0x00'01'34: MaskedWrite(sys->RCR, wrdata, mask & 0x83); break;


    case 0x00'02'00:
        MaskedWrite(sys->IE7, wrdata, mask & 0x1FFF);
        IF7_Clear(sys, (wrdata & mask) >> 16, now);
        //Sched_AddEvent(sys, now+DSClk16(1), Evt_UpdateIRQ7); called by IF7_Clear
        break;

    case 0x00'02'04: MaskedWrite(sys->ExtMemCR_AGB.Raw, wrdata, mask&0x6FFF); break;

    case 0x00'02'08:
        MaskedWrite(sys->IME7, wrdata, mask & 1);
        Sched_AddEvent(sys, now+DSClk16(1), Evt_UpdateIRQ7);
        break;

    case 0x00'03'00:
        if (sys->Bios7ProtCur < 0x4000) // can only be written from bios. for... some reason?
        {
            if (mask & 0x1) sys->PostFlag |= wrdata & 0x1;
            if (mask & 0xFF00) // wait control
            {
                switch((wrdata >> 15) & 0x1)
                {
                case 0: // halt; stop clocking arm7tdmi until IE & IF
                    if (!Console_CheckARM7Wake(sys)) // checkme: might still halt for a little?
                        sys->A7ClkDisable = true;
                    break;
                case 1: // sleep
                    // this should be similar to halt but disabling a bunch more hardware...?
                    LogPrint(LOG_ARM7|LOG_UNIMP, "I dont really know what sleep does but it's not in yet!\n");
                    break;
                }
            }
        }
        break;

    case 0x00'03'04: MaskedWrite(sys->PowerCR7.Raw, wrdata, mask & 0x3); break;

    default:
        LogPrint(LOG_ARM7 | LOG_UNIMP | LOG_IO, "UNIMPLEMENTED IOAGB WRITE: %08"PRIX32" %08"PRIX32" %08"PRIX32" @ %08"PRIX32"\n", addr, wrdata, mask, sys->A7TDMI.ARM.PC);
        break;
    }
    LogPrint(LOG_IOALL|LOG_ARM7, "IOAGB WRITE: ADDR:%08X WRDATA:%08X LANES:%08X\n", addr, wrdata, mask);

    AddBusContention(sys, now, Dev_IO7);
    Bus_TransferPostSetup(sys, 0, false, now, false, false);
}

void IOAGB_Handler(Console* sys, timestamp now)
{
    BusReq* req = &sys->Bus7.PipeFIFO[sys->Bus7.ReqActivePtr];
    const u32 addr = req->Addr;

    if (req->Write) IOAGB_Write(sys, addr, now, req->WrData, MakeWriteMask(addr, req->Size));
    else            IOAGB_Read (sys, addr, now);
}
