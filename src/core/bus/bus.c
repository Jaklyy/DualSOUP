#include <assert.h>
#include <stdbit.h>
#include <stddef.h>
#include "bus.h"
#include "core/arm/arm9/arm.h"
#include "core/utils.h"
#include "core/io/dma.h"
#include "core/console.h"
#include "core/carts/gamepak.h"
#include "core/video/video.h"
#include "core/scheduler.h"
#include "vram.h"



void Bus9_Init(BusImpl* bus)
{
    bus->PipeCycles = DSClk33(3);
}

void Bus7_Init(BusImpl* bus)
{
    bus->PipeCycles = DSClk33(1);
}

void MainRAM_Init(Console* sys, NTRFCRAM fcramsize)
{
    switch(fcramsize)
    {
    case NTRFCRAM_4MiB:  sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(4)-1)); break;
    case NTRFCRAM_8MiB:  sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(8)-1)); break;
    case NTRFCRAM_16MiB: sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(16)-1)); break;
    case NTRFCRAM_32MiB: sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(32)-1)); break;
    }
}

timestamp BusContention(Console* sys, timestamp cur, const NTRAHB_Devices device)
{
    // check if the device we're accessing is busy
    // sequential accesses shouldn't need to be checked on
    if (sys->AHBBusyTS[device] > cur)
        return sys->AHBBusyTS[device] - cur;
    else return 0;
}

void AddBusContention(Console* sys, const timestamp cur, const NTRAHB_Devices device)
{
    sys->AHBBusyTS[device] = cur+DSClk33(1);
}


// Welcome to my special little hell. :D
void MainRAM_Request(Console* sys, timestamp now, const bool a9)
{
    BusMainRAM* mr = &sys->BusMR;

    if (a9) mr->IsReq9 = true;
    else    mr->IsReq7 = true;

    DS_CLAMP(now, <, mr->LastFetchTs)
    Sched_AddEvent(sys, now, Evt_MainRAM);
}

bool MainRAM_KillBurst(Console* sys, timestamp now)
{
    BusMainRAM* mr = &sys->BusMR;

    if (mr->BurstActive)
    {
        // stop main ram burst if still running
        if (mr->PrevWrite) now += DSClk33(5); // stores: 5 cycle cooldown period
        else               now += DSClk33(3); // loads:  3 cycle cooldown period

        if (mr->IsReq9 || mr->IsReq7) Sched_AddEvent(sys, now, Evt_MainRAM);
        mr->BurstLimitTs = timestamp_max;
        mr->LastFetchTs = now;
        mr->BurstActive = false;
        mr->CurReq = MainRAM_None;
        return true;
    }
    return false; // burst was already terminated
}

void MainRAM_Run(Console* sys, timestamp now)
{
    BusMainRAM* mr = &sys->BusMR;
    MainRAM_Buses grant = MainRAM_None;

    if (now > mr->BurstLimitTs) { MainRAM_KillBurst(sys, now); return; }

    if (mr->Locked != MainRAM_None) // main ram interface respects atomic lock signals
    {
        // make sure there's an actual req happening first, just in case; CHECKME: is this check useful?
        if (mr->Locked == MainRAM_A9)
        {
            if (mr->IsReq9) grant = MainRAM_A9;
            else LogPrint(LOG_ARM9|LOG_FCRAM, "ARM9 Atomic access to fcram but no req?\n");
        }
        else // arm7
        {
            if (mr->IsReq7) grant = MainRAM_A7;
            else LogPrint(LOG_ARM7|LOG_FCRAM, "ARM7 Atomic access to fcram but no req?\n");
        }
    }
    else if (sys->ExtMemCR_Shared.MRA7Priority)
    {
        // check arm7 req first
        if      (mr->IsReq7) grant = MainRAM_A7;
        else if (mr->IsReq9) grant = MainRAM_A9;
    }
    else
    {
        // check arm9 req first
        if      (mr->IsReq9) grant = MainRAM_A9;
        else if (mr->IsReq7) grant = MainRAM_A7;
    }

    if (grant == MainRAM_None) { MainRAM_KillBurst(sys, now); return; }

    BusReq* r = ((grant == MainRAM_A9) ? (&sys->Bus9.PipeFIFO[sys->Bus9.FIFODrainPtr])
                                       : (&sys->Bus7.PipeFIFO[sys->Bus7.FIFODrainPtr]));
    AHB_HSIZE size = r->Size;
    u32 addr = (r->Addr >> size) << size; // make sure addr is aligned for word fetches
    bool nseq = r->Type == HTRANS_NONSEQ;
    bool write = r->Write;
    bool lock = r->Lock;
    u32 wrval; if (write) wrval = r->WrVal;

    if (write && (addr & 2)) wrval >>= 16;

    if (write != mr->PrevWrite) nseq = true; // split burst if switching from read to write
    mr->PrevWrite = write;
    if (grant != mr->CurReq) nseq = true; // split burst if switching which bus has grant
    mr->CurReq = grant;
    if (now > mr->BurstLimitTs) nseq = true; // TODO: this should probably be regardless of an access occuring

    // this is presumably enforced by the SoC main ram interface?
    // so im not sure if it actually uses the address signaled on the bus, or if it latches it internally somehow?
    // this may or may not actually matter?
    if (mr->WeirdStart && !nseq && !(addr & 0x1E)) nseq = true;

    if (nseq && MainRAM_KillBurst(sys, now)) return;

    if (lock) mr->Locked = grant;
    else mr->Locked = false;

#define MRStepAddr mr->AddrLatch = (mr->AddrLatch + 1) & mr->AddrLatchMask;

    if (nseq)
    {
        mr->AddrLatch = (addr & mr->AddrSubmMask) >> 1;
        mr->WeirdStart = ((addr & 0x1E) >= 0x1A);
        mr->BurstLimitTs = now + DSClk33(241);
    }
    else MRStepAddr

    u32 rdata;
    if (write)
    {
        rdata = 0;
        if (size == HSIZE_8)
        {
            if (!nseq) CrashSpectacularly("SEQUENTIAL 8 BIT MAIN RAM WRITE????????????\n");
            MaskedWrite(sys->MainRAM.b16[mr->AddrLatch], wrval, 0xFF << ((addr & 1)*8));
            now += DSClk33(4); // takes longer for some reason
            MainRAM_KillBurst(sys, now + DSClk33(3)); // CHECKME: it takes less time for it to cooldown, so i assume it does it early somehow??
        }
        else
        {
            sys->MainRAM.b16[mr->AddrLatch] = wrval & 0xFFFF;
            if (size == HSIZE_32)
            {
                MRStepAddr
                sys->MainRAM.b16[mr->AddrLatch] = wrval >> 16;
                now += DSClk33(4);
            }
            else now += DSClk33(3);
        }
    }
    else // read
    {
        rdata = sys->MainRAM.b16[mr->AddrLatch];
        if (size < HSIZE_32)
        {
            now += (nseq) ? DSClk33(5) : DSClk33(1);
            rdata |= rdata << 16; // mirror onto both halves of word
        }
        else // 32 bit; do another fetch for high bytes
        {
            now += ((nseq)  ? DSClk33(6)
                            : ((now == mr->LastFetchTs) // questionably emulate read prefetching
                                ? DSClk33(2)
                                : DSClk33(1)));
            MRStepAddr
            rdata |= sys->MainRAM.b16[mr->AddrLatch] << 16;
        }
    }

    mr->LastFetchTs = now;

    if (grant == MainRAM_A9) mr->IsReq9 = false;
    else                     mr->IsReq7 = false;
    Bus_TransferPostSetup(sys, rdata, !write, now, false, r->CB, (grant == MainRAM_A9));

    if (size != HSIZE_8) // this special casing is stupid but i dont wanna fix it
        Sched_AddEvent(sys, mr->BurstLimitTs, Evt_MainRAM); // schedule an event to enforce burst limit
}
#undef MRStepAddr

void Bus_VRAM(Console* sys, u32* rdata, timestamp* now, u32 addr, const BusReq* req, const bool a9)
{
    const struct
    {
        u16* bank;
        size_t size;
    } vram[VRAMID_MAX] =
    {
        {sys->VRAM_A.b16, VRAM_A_Size},
        {sys->VRAM_B.b16, VRAM_B_Size},
        {sys->VRAM_C.b16, VRAM_C_Size},
        {sys->VRAM_D.b16, VRAM_D_Size},
        {sys->VRAM_E.b16, VRAM_E_Size},
        {sys->VRAM_F.b16, VRAM_F_Size},
        {sys->VRAM_G.b16, VRAM_G_Size},
        {sys->VRAM_H.b16, VRAM_H_Size},
        {sys->VRAM_I.b16, VRAM_I_Size}
    };

    u16 list;
    if (a9)
    {
        switch((addr >> 20) & 0xE)
        {
            case 0:  list = VRAM_BGA (sys, addr); break;
            case 2:  list = VRAM_BGB (sys, addr); break;
            case 4:  list = VRAM_OBJA(sys, addr); break;
            case 6:  list = VRAM_OBJB(sys, addr); break;
            default: list = VRAM_LCD (sys, addr); break;
        }
    }
    else list = VRAM_ARM7(sys, addr);

    if (!list)
    {
        // nobody's home
        *now += DSClk33(1);
        if (!req->Write) *rdata = 0;
    }
    else if (stdc_count_ones(list) <= 1)
    {
        u8 id = stdc_trailing_zeros(list);
        u16* bank = vram[id].bank;
        size_t size = vram[id].size;
        // 1 region, use simpler logic
        // TODO: just use lut for this case?
        if (req->Write)
        {
            u32 wrdata = req->WrVal;
            // TODO: PPU contention
            *now += DSClk33(1);
            PPU_Sync(sys, *now); // TODO: DO THIS BETTER
            if (req->Size < HSIZE_32)
            {
                wrdata = ROR32(wrdata, (addr & 2) * 8);
                if (req->Size == HSIZE_8)
                {
                    MaskedWrite(bank[(addr & (size-1))/2], wrdata, 0xFF << ((addr&1)*8));
                }
                bank[addr & (size-1)/2] = wrdata;
            }
            else
            {
                addr &= ~3;
                bank[(addr & (size-1))/2] = wrdata;
                *now += DSClk33(1);
                PPU_Sync(sys, *now); // TODO: DO THIS BETTER
                bank[((addr+2) & (size-1))/2] = wrdata >> 16;
            }
            AddBusContention(sys, *now, Dev_VRAM_A + id);
        }
        else
        {
            // TODO: PPU contention
            *now += DSClk33(1) + BusContention(sys, *now, Dev_VRAM_A + id);
            if (req->Size < HSIZE_32)
            {
                *rdata = bank[(addr & (size-1))/2];
                *rdata |= *rdata << 16;
            }
            else
            {
                addr &= ~3;
                *rdata = bank[(addr & (size-1))/2];
                *now += DSClk33(1);
                *rdata |= bank[((addr+2) & (size-1))/2] << 16;
            }
        }
    }
    else // overlap; slow handler
    {
        u16 list2 = list;
        u16 contlist = 0;
        // find which ones have contention
        while (list2)
        {
            u8 id = stdc_trailing_zeros(list2);
            if (BusContention(sys, *now, Dev_VRAM_A + id))
                contlist |= 1<<id;

            list2 &= (~1)<<id;
        }
        list2 = list;
        // now interate through handling reads/writes
        // TODO: This is probably all wrong and going to need a rewrite to fix a lot of shit. especially writes and ppu interaction
        if (req->Write)
        {
            const u32 wrdata = req->WrVal;
            *now += DSClk33((req->Size < HSIZE_32) ? 1 : 2); // dumb
            PPU_Sync(sys, *now); // no!
            while (list2)
            {
                u32 tmpwrdata = wrdata;
                u8 id = stdc_trailing_zeros(list2);
                u16* bank = vram[id].bank;
                size_t size = vram[id].size;
                if (req->Size < HSIZE_32)
                {
                    tmpwrdata = ROR32(tmpwrdata, (addr & 2) * 8);
                    if (req->Size == HSIZE_8)
                    {
                        MaskedWrite(bank[(addr & (size-1))/2], tmpwrdata, 0xFF << ((addr&1)*8));
                    }
                    bank[addr & (size-1)/2] = tmpwrdata;
                }
                else
                {
                    addr &= ~3;
                    bank[(addr & (size-1))/2] = tmpwrdata;
                    bank[((addr+2) & (size-1))/2] = tmpwrdata >> 16;
                }
                AddBusContention(sys, *now, Dev_VRAM_A + id); // TODO: busy ones might resolve their writes before unbusy ones...?
                list2 &= (~1)<<id;
            }
        }
        else
        {
            *now += DSClk33((req->Size < HSIZE_32) ? 1 : 2); // dumb
            if (contlist)
            {
                *now += 1; // TODO: this is handled differently than other contention causes; fix that.
                list2 &= contlist; // yes, this is correct(ish)
            }
            *rdata = 0;
            while (list2)
            {
                u8 id = stdc_trailing_zeros(list2);
                u16* bank = vram[id].bank;
                size_t size = vram[id].size;
                if (req->Size < HSIZE_32)
                {
                    *rdata |= bank[(addr & (size-1))/2];
                    *rdata |= *rdata << 16;
                }
                else
                {
                    addr &= ~3;
                    *rdata |= bank[(addr & (size-1))/2];
                    *rdata |= bank[((addr+2) & (size-1))/2] << 16;
                }
                list2 &= (~1)<<id;
            }
        }
    }
}

void GamePakBus_ROMRead(Console* sys, u32* rdata, timestamp* now, const u32 addr, const AHB_HSIZE size, const bool a9)
{
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        *now += DSClk33(1); // TODO
        if (size == HSIZE_32)
        {
            *rdata = GamePak_ROMRead(&sys->GamePak, addr & ~3);
            *rdata |= GamePak_ROMRead(&sys->GamePak, (addr & ~3) | 2) << 16;
        }
        else
        {
            *rdata = GamePak_ROMRead(&sys->GamePak, addr);
            *rdata |= *rdata << 16;
        }
    }
    else // unmapped
    {
        *now += DSClk33(1); // checkme: should this use configured waitstates?
        *rdata = 0;
    }
}
void GamePakBus_ROMWrite(Console* sys, const u32 wrdata, timestamp* now, const u32 addr, const AHB_HSIZE size, const bool a9)
{
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        *now += DSClk33(1); // TODO
        GamePak_ROMWrite(&sys->GamePak, addr, wrdata);
        if (size == HSIZE_32) // TODO: how does this actually work?
            GamePak_ROMWrite(&sys->GamePak, addr+2, wrdata);
    }
    else *now += DSClk33(1); // unmapped; checkme: should this use configured waitstates?
}

void GamePakBus_RAMRead(Console* sys, u32* rdata, timestamp* now, const u32 addr, const AHB_HSIZE size, const bool a9)
{
    // note: 8 bit bus, only supports byte reads, does not ignore low bits of address for larger accesses.
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        if (size != HSIZE_8) LogPrint((a9 ? LOG_ARM9 : LOG_ARM7)|LOG_ODD|LOG_PAK, "NTR_Bus%"PRIu8": %"PRIu32" bit read from GBA Game Pak SRAM, width > 8 bit are weird, probably not correct?\n", 7+(a9*2), 8<<size);
        *now += DSClk33(1); // TODO
        *rdata = GamePak_SRAMRead(&sys->GamePak, addr);
    }
    else // unmapped
    {
        *now += DSClk33(1); // checkme: should this use configured waitstates?
        *rdata = 0; // always returns 0
    }
    *rdata = *rdata | (*rdata << 8) | (*rdata << 16) | (*rdata << 24); // byte is mirrored across all bus lanes.
}
void GamePakBus_RAMWrite(Console* sys, const u32 wrdata, timestamp* now, const u32 addr, const AHB_HSIZE size [[maybe_unused]], const bool a9)
{
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        //if (size != HSIZE_8) LogPrint((a9 ? LOG_ARM9 : LOG_ARM7)|LOG_ODD|LOG_PAK, "NTR_Bus%"PRIu8": %"PRIu32" bit write to GBA Game Pak SRAM, width > 8 bit are weird, probably not correct?\n", 7+(a9*2), 8<<size);
        *now += DSClk33(1); // TODO
        GamePak_SRAMWrite(&sys->GamePak, addr, ROR32(wrdata, 8*addr) /* select proper byte lanes */); // CHECKME
    }
    else *now += DSClk33(1); // unmapped; checkme: should this use configured waitstates?
}

void Bus9_Read(Console* sys, BusReq* req, timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    if (size > HSIZE_32) CrashSpectacularly("ARM9 AHB READ TOO WIDE: %"PRIu32"\n", width);

    u32 rdata;
    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        return MainRAM_Request(sys, now, true); // defer completion of req to main ram handler

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        // Speculation: like still writing the wram interface? and that's just swallowing the read/write?
        now += DSClk33(1) + BusContention(sys, now, Dev_WRAM9);
        switch(sys->WRAMCR)
        {
            case 0: rdata = MemoryRead(32, sys->SharedWRAM,   addr, SharedWRAM_Size  ); break;
            case 1: rdata = MemoryRead(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2); break;
            case 2: rdata = MemoryRead(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2); break;
            case 3: rdata = 0; break; // unmapped
            default: unreachable();
        }
        break;

    case 0x04: // Memory Mapped IO
        // checkme: does all of IO have write contention at the same time?
        // checkme: does all of IO have the exact same timings?
        // checkme: contention would be first here, yes?
        return Sched_AddEvent(sys, now + BusContention(sys, now, Dev_IO9), Evt_IO9); // io is in a separate handler for simplicity's sake

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE READ?\n");
            now += DSClk33(1);
            rdata = 0;
        }
        else
        {
            PPU_Sync(sys, now); // TODO: rework this shit
            now += DSClk33(1) + BusContention(sys, now, Dev_Palette);
            if (size >= HSIZE_32)
            {
                PPU_Sync(sys, now);
                now += DSClk33(1) + BusContention(sys, now, Dev_Palette);
                // should technically be two separate reads
                // not sure if that actually matters?
                rdata = MemoryRead(32, sys->Palette, addr, Palette_Size);
            }
            else
            {
                rdata = MemoryRead(16, sys->Palette, addr, Palette_Size);
                rdata |= rdata << 16; // fetch is mirrored in both halves
            }
        }
        break;

    case 0x06: // VRAM
        Bus_VRAM(sys, &rdata, &now, addr, req, true); break;

    case 0x07: // 2D GPU OAM
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED OAM READ?\n");
            now += DSClk33(1);
            rdata = 0;
        }
        else
        {
            now += DSClk33(1) + BusContention(sys, now, Dev_Palette);
            rdata = MemoryRead(32, sys->OAM, addr, OAM_Size);
        }
        break;

    case 0x08 ... 0x09: // GBA Game Pak ROM
        GamePakBus_ROMRead(sys, &rdata, &now, addr, size, true); break;

    case 0x0A: // GBA Game Pak SRAM
        GamePakBus_RAMRead(sys, &rdata, &now, addr, size, true); break;

    case 0xFF: // NDS BIOS
        if ((addr & 0xFFFFF000) == 0xFFFF0000)
        {
            // bios does not have contention, interestingly enough.
            now += DSClk33(1);
            rdata = MemoryRead(32, sys->NTRBios9, addr, NTRBios9_Size);
            break;
        }
        else { [[fallthrough]]; }

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %"PRIu32" bit read from unmapped memory at 0x%08"PRIX32"? Something went wrong?\n", width, addr);
        now += DSClk33(1);
        rdata = 0; // always reads 0
        break;
    }

    Bus_TransferPostSetup(sys, rdata, true, now, false, req->CB, true);
}

void Bus9_Write(Console* sys, BusReq* req, timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    const u32 mask = MakeWriteMask(addr, size);
    const u32 wrdata = req->WrVal;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    if (size > HSIZE_32) CrashSpectacularly("ARM9 BUS READ TOO WIDE: %"PRIu32"\n", width);

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        return MainRAM_Request(sys, now, true); // defer completion of req to main ram handler

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        now += DSClk33(1);
        AddBusContention(sys, now, Dev_WRAM9);
        switch(sys->WRAMCR)
        {
            case 0: MemoryWrite(32, sys->SharedWRAM  , addr, SharedWRAM_Size  , wrdata, mask); break;
            case 1: MemoryWrite(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2, wrdata, mask); break;
            case 2: MemoryWrite(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2, wrdata, mask); break;
            case 3: break;
            default: unreachable();
        }
        break;

    case 0x04: // Memory Mapped IO
        return Sched_AddEvent(sys, now + DSClk33(1), Evt_IO9); // io is in a separate handler for simplicity's sake

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower) || (size == HSIZE_8))
        {
            if (size == HSIZE_8) LogPrint(LOG_ARM9|LOG_ODD, "8 BIT PALETTE WRITE?\n");
            else                 LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE WRITE?\n");
            now += DSClk33(1);
            // CHECKME: contention for bytes?
        }
        else
        {
            if (mask & 0x0000FFFF)
            {
                now += DSClk33(1);
                PPU_Sync(sys, now);
                //BusContention(sys->AHBBusyTS, &sys->AHB9.Timestamp, Dev_Palette);
                //AddBusContention(sys->AHBBusyTS, sys->AHB9.Timestamp, Dev_Palette);

                MemoryWrite(32, sys->Palette, addr, Palette_Size, wrdata, mask & 0x0000FFFF);
            }
            if (mask & 0xFFFF0000)
            {
                now += DSClk33(1);
                PPU_Sync(sys, now);
                //BusContention(sys->AHBBusyTS, &sys->AHB9.Timestamp, Dev_Palette);
                //AddBusContention(sys->AHBBusyTS, sys->AHB9.Timestamp, Dev_Palette);

                MemoryWrite(32, sys->Palette, addr, Palette_Size, wrdata, mask & 0xFFFF0000);
            }
        }
        break;

    case 0x06: // VRAM
        // TODO: 2d gpu contention timings
        if (size == HSIZE_8)
        {
            LogPrint(LOG_ARM9|LOG_ODD, "ARM9: 8 BIT VRAM WRITE?\n");
            now += DSClk33(1);
            // CHECKME: contention for bytes?
        }
        else Bus_VRAM(sys, nullptr, &now, addr, req, true);
        break;

    case 0x07: // 2D GPU OAM
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower) || (width == 8))
        {
            // for some reason oam doesn't support byte writes
            // CHECKME: does it support halfwords?
            if (size == HSIZE_8) LogPrint(LOG_ARM9|LOG_ODD, "8 BIT OAM WRITE?\n");
            else                 LogPrint(LOG_ARM9|LOG_ODD, "DISABLED OAM WRITE?\n");
            now += DSClk33(1);
            // CHECKME: contention for bytes?
        }
        else
        {
            now += DSClk33(1);
            PPU_Sync(sys, now);
            AddBusContention(sys, now, Dev_OAM);
            MemoryWrite(32, sys->OAM, addr, OAM_Size, wrdata, mask);
        }
        break;

    case 0x08 ... 0x09: // GBA Game Pak ROM
        GamePakBus_ROMWrite(sys, wrdata, &now, addr, size, true); break;

    case 0x0A: // GBA Game Pak SRAM
        GamePakBus_RAMWrite(sys, wrdata, &now, addr, size, true); break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %"PRIu32" bit write to unmapped memory at 0x%08"PRIX32"? Something went wrong?\n", width, addr);
        now += DSClk33(1);
        break;
    }

    Bus_TransferPostSetup(sys, 0, false, now, false, req->CB, true);
}

void Bus7_Read(Console* sys, BusReq* req, timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;

    if (size > HSIZE_32) CrashSpectacularly("ARM7 BUS READ TOO WIDE: %"PRIu32"\n", width);
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    if (!req->Prot.Data && (req->Man7 == MAN7_ARM7)) // code access by ARM7TDMI
    {
        // set bios protection level depending on if bios is selected by arbiter and word address sent to bios rom interface
        if (addr >= 0x4000) sys->Bios7ProtCur = 0x4000; // bios fully protected
        else if (addr >= sys->Bios7Prot) sys->Bios7ProtCur = sys->Bios7Prot;
    }

    u32 rdata;
    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x000: // ARM7 BIOS
        if (addr < 0x4000)
        {
            // CHECKME: does bios7 write contention work weirdly with bios prot?
            // CHECKME: does bios7 write contention exist?
            //BusContention(sys->AHBBusyTS, &sys->AHB7.Timestamp, Dev_Bios7);
            now += DSClk33(1);

            // a7 bios reads have protection
            // TODO: contemplate exact bios protection mechanism further
            // non-arm7 accesses are rejected entirely
            // CHECKME: this may or may not avoid accessing the bus entirely judging by gba dma behavior?
            if ((req->Man7 != MAN7_ARM7) || addr < sys->Bios7ProtCur)
            {
                rdata = 0xFFFFFFFF; // returns all 1s for some reason?
                LogPrint(LOG_ARM7|LOG_ODD, "Protected Bios7 Read? Addr: %08"PRIX32"\n", addr);
            }
            else rdata = MemoryRead(32, sys->NTRBios7, addr, NTRBios7_Size);
            break;
        }
        else { [[fallthrough]]; }

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM7, "NTR Bus7: %"PRIu32" bit read from unmapped memory at %08"PRIX32"? Something went wrong?\n", width, addr);
        now += DSClk33(1);
        rdata = 0; // always reads 0
        break;

    case 0x020 ... 0x028: // Main RAM
        return MainRAM_Request(sys, now, false); // defer completion of req to main ram handler

    case 0x030: // Shared WRAM
        now += DSClk33(1) + BusContention(sys, now, Dev_WRAM7);
        // CHECKME: it might be possible that contention could delay wram access until after its remapped by arm9?
        switch(sys->WRAMCR)
        {
        case 0: rdata = MemoryRead(32, sys->ARM7WRAM,     addr, ARM7WRAM_Size    ); break;
        case 1: rdata = MemoryRead(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2); break;
        case 2: rdata = MemoryRead(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2); break;
        case 3: rdata = MemoryRead(32, sys->SharedWRAM,   addr, SharedWRAM_Size  ); break;
        default: unreachable();
        }
        break;

    case 0x038: // ARM7 WRAM
        now += DSClk33(1) + BusContention(sys, now, Dev_WRAM7 /* checkme: i think this is wrong actually...? */);
        rdata = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size);
        break;

    case 0x040: // Memory Mapped IO
        return Sched_AddEvent(sys, now + BusContention(sys, now, Dev_IO7), Evt_IO7); // io is in a separate handler for simplicity's sake

    case 0x048: // WiFi
        WiFi_Read(sys, &rdata, &now, addr, size); break;

    case 0x060 ... 0x068: // VRAM
        Bus_VRAM(sys, &rdata, &now, addr, req, false); break;

    case 0x080 ... 0x098: // GBA Game Pak ROM
        GamePakBus_ROMRead(sys, &rdata, &now, addr, size, false); break;

    case 0x0A0 ... 0x0A8: // GBA Game Pak SRAM
        GamePakBus_RAMRead(sys, &rdata, &now, addr, size, false); break;
    }

    Bus_TransferPostSetup(sys, rdata, true, now, false, req->CB, false);
}

void Bus7_Write(Console* sys, BusReq* req, timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    const u32 mask = MakeWriteMask(addr, size);
    const u32 wrdata = req->WrVal;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    /*case 0x000: // ARM7 BIOS
        if (timings && (addr < 0x4000))
        {
            // CHECKME: does bios7 write contention work weirdly with bios prot?
            Timing32(&sys->AHB7);
            AddBusContention(sys->AHBBusyTS, sys->AHB7.Timestamp, Dev_Bios7);
        }
        break;*/

    case 0x020 ... 0x028: // Main RAM
        return MainRAM_Request(sys, now, false); // defer completion of req to main ram handler

    case 0x030: // Shared WRAM
        now += DSClk33(1);
        AddBusContention(sys, now, Dev_WRAM7);
        switch(sys->WRAMCR)
        {
            case 0: MemoryWrite(32, sys->ARM7WRAM,     addr, ARM7WRAM_Size,     wrdata, mask); break;
            case 1: MemoryWrite(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2, wrdata, mask); break;
            case 2: MemoryWrite(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2, wrdata, mask); break;
            case 3: MemoryWrite(32, sys->SharedWRAM ,  addr, SharedWRAM_Size,   wrdata, mask); break;
            default: unreachable();
        }
        break;

    case 0x038: // ARM7 WRAM
        now += DSClk33(1);
        AddBusContention(sys, now, Dev_WRAM7 /* checkme: probably wrong? */);
        MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, wrdata, mask);
        break;

    case 0x040: // Memory Mapped IO
        return Sched_AddEvent(sys, now + DSClk33(1), Evt_IO7); // io is in a separate handler for simplicity's sake

    case 0x048: // WiFi
        WiFi_Write(sys, &now, addr, wrdata, mask); break;

    case 0x060 ... 0x068: // VRAM
        Bus_VRAM(sys, nullptr, &now, addr, req, false); break;

    case 0x080 ... 0x098: // GBA Game Pak ROM
        GamePakBus_ROMWrite(sys, wrdata, &now, addr, size, false); break;

    case 0x0A0 ... 0x0A8: // GBA Game Pak SRAM
        GamePakBus_RAMWrite(sys, wrdata, &now, addr, size, false); break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM7,"NTR_AHB7: %"PRIu32" bit write to unmapped memory at 0x%08"PRIX32"? Something went wrong?\n", width, addr);
        now += DSClk33(1);
        break;
    }

    Bus_TransferPostSetup(sys, 0, false, now, false, req->CB, false);
}

void Bus9_Idle(Console* sys, const timestamp now)
{
    // no access performed this cycle
    // speculation: treat as signal to kill bursts
    if (sys->BusMR.CurReq == MainRAM_A9)
        MainRAM_KillBurst(sys, now);

    // TODO: kill gba rom/ram chipsel
}

void Bus9_Busy(Console* sys [[maybe_unused]], const timestamp now [[maybe_unused]])
{
    // no access performed this cycle
    // speculation: do *not* kill bursts
}

void Bus7_Idle(Console* sys, const timestamp now)
{
    // no access performed this cycle
    // speculation: treat as signal to kill bursts
    if (sys->BusMR.CurReq == MainRAM_A7)
        MainRAM_KillBurst(sys, now);

    // TODO: kill gba rom/ram chipsel
}

void Bus7_Busy(Console* sys [[maybe_unused]], const timestamp now [[maybe_unused]])
{
    // no access performed this cycle
    // speculation: do *not* kill bursts
}

void Bus_Req(Console* sys, const BusReq* req, const timestamp now, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);

    bus->ReqList |= (1<<(req->Man));
    bus->Reqs[req->Man] = *req;

    if (!bus->LockSched) Sched_AddEventIfEarlier(sys, now, a9 ? Evt_Bus9 : Evt_Bus7);
}

void Bus7_A7Wake(Console* sys, const timestamp now)
{
    if (!sys->Bus7.LockSched && (sys->Bus7.ReqList & (1<<MAN7_ARM7))) Sched_AddEventIfEarlier(sys, now, Evt_Bus7);
}

void Bus_TransferPostSetup(Console* sys, const u32 rdata, const bool isread, const timestamp end, const bool noprev, const bool cb, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);
    if (isread) bus->PostReadBus = rdata;
    bus->PostNoPrev = noprev;
    bus->PostCB = cb;
    Sched_AddEvent(sys, end, a9 ? Evt_Bus9HReady : Evt_Bus7HReady);
}

void Bus_TransferPost(Console* sys, const timestamp fin, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);
    u32 rdata = bus->PostReadBus;
    BusCallbacks cmpcb = bus->PostCB;
    const timestamp len = (bus->PostNoPrev ? DSClk33(1) : (fin - bus->PipeExitTs[bus->FIFODrainPtr]));
    // ahb pipeline steps once every HREADY

    bus->FIFODrainPtr = (bus->FIFODrainPtr + 1) % countof(bus->PipeFIFO);

    if (bus->FIFODrainPtr == bus->FIFOFillPtr)
        bus->FIFOEmpty = true;

    // apply waitstate delays
    if (len > DSClk33(1))
    {
        for (s32 i = 0; i < 4; i++)
            bus->PipeExitTs[i] += (len-DSClk33(1));
    }

    // speculative method of implementing arm7 halt
#define reqlista7deny (((!a9 && sys->A7ClkDisable) ? ((1<<MAN7_ARM7)-1) : u32_max) & bus->ReqList)


    BusCallbacks ackcb = CB_None;
    // arbitrate next req
    if (reqlista7deny)
    {
        u8 manager;
        // handle locked transfers
        if (bus->HLockGeneric == MAN_NONE)
            manager = stdc_trailing_zeros(reqlista7deny); // not locked
        else
        {
            manager = bus->HLockGeneric; // locked, manager stays the original

            if (!(bus->ReqList & (1<<manager))) // make sure it's actually trying to do a transfer
                goto nvm; // assume its holding lock with idle transfers
        }

        // put entry into fifo
        bus->PipeFIFO[bus->FIFOFillPtr] = bus->Reqs[manager];
        bus->PipeExitTs[bus->FIFOFillPtr] = fin + bus->PipeCycles;
        ackcb = bus->Reqs[manager].CB;

        bus->ReqList &= ~(1<<manager); // clear req list
        // update lock flag
        bus->HLockGeneric = ((bus->PipeFIFO[bus->FIFOFillPtr].Lock) ? manager : MAN_NONE);

        // step fill ptr
        bus->FIFOFillPtr = (bus->FIFOFillPtr + 1) % countof(bus->PipeFIFO);
        bus->FIFOEmpty = false;
    }
    nvm:

    // schedule next event
    timestamp new;
    // check if something is still in req list
    if (bus->HLockGeneric ? (bus->ReqList & (1<<bus->HLockGeneric)) : reqlista7deny)
    {
        // step pipeline 1 cycle
        new = fin + DSClk33(1);
    }
    else if (!bus->FIFOEmpty)
    {
        // if something is in the pipeline wait for it
        new = bus->PipeExitTs[bus->FIFODrainPtr];
    }
    else
    {
        // nothing to do; ahb go nini
        goto noresched;
    }
    Sched_AddEvent(sys, new, a9 ? Evt_Bus9 : Evt_Bus7);
    noresched:
    bus->LockSched = false; // fetch completed; we can allow scheduling again

    // ack callback
    switch(ackcb)
    {
    case CB_None: break;
    case CB9_BIU9InstrNormal ... CB9_BIU9Idle: A946_BIUSubmPost(&sys->A946ES, fin); break;
    }

    // completion callback
    switch(cmpcb)
    {
    case CB_None: break;
    case CB9_BIU9InstrNormal ... CB9_BIU9Idle: A946_BIUCompPost(&sys->A946ES, fin, rdata, cmpcb); break;
    }
}

void Bus_Run(Console* sys, const timestamp now, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);
    bus->LockSched = true; // make sure we can't reschedule shit until *after* the fetch completes

    // step pipeline
    if (!bus->FIFOEmpty)
    {
        BusReq* req = &bus->PipeFIFO[bus->FIFODrainPtr];
        if ((bus->PipeExitTs[bus->FIFODrainPtr] <= now))
        {
            if (req->Type >= HTRANS_NONSEQ)
            {
                // its time; begin transfer!
                if (a9)
                {
                    if (req->Write) Bus9_Write(sys, req, now);
                    else            Bus9_Read(sys, req, now);
                }
                else
                {
                    if (req->Write) Bus7_Write(sys, req, now);
                    else            Bus7_Read(sys, req, now);
                }
                return; // rest of logic is for handling 
            }
            else if (req->Type == HTRANS_BUSY)
            {
                if (a9) Bus9_Busy(sys, now);
                else    Bus7_Busy(sys, now);
            }
            else goto idle; // explicit idle transfer
        }
        else goto idle; // not time yet; implied idle transfer
    }
    else // nothing running; implied idle transfer
    { idle:
        if (a9) Bus9_Idle(sys, now);
        else    Bus7_Idle(sys, now);
    }

    Bus_TransferPostSetup(sys, 0, false, now + DSClk33(1), true, CB_None, a9);
}
