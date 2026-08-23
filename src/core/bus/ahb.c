#include <stdbit.h>
#include <stddef.h>
#include "ahb.h"
#include "core/utils.h"
#include "core/io/dma.h"
#include "core/console.h"
#include "core/carts/gamepak.h"
#include "core/video/video.h"
#include "core/scheduler.h"
#include "vram.h"



u32 MakeWriteMask(u32 addr, AHB_HSIZE size)
{
    // TODO: test if one of these approaches is actually meaningfully faster
#if 0
    const u32 width = 8<<size;
    const u32 mask = ROL32(((s64)-0x100000000 >> width), ((addr & 0x3) * 8) + 1);
    return mask;
#else
    switch(size)
    {
        case HSIZE_8:  return 0xFF   << (addr & 3) * 8;
        case HSIZE_16: return 0xFFFF << (addr & 3) * 8;
        case HSIZE_32: return 0xFFFFFFFF;
        default: unreachable();
    }
#endif
}

timestamp BusContention(timestamp* busyts, timestamp cur, const NTRAHB_Devices device)
{
    // check if the device we're accessing is busy
    // sequential accesses shouldn't need to be checked on
    if (busyts[device] > cur)
        return busyts[device] - cur;
    else return 0;
}

void AddBusContention(timestamp* busyts, const timestamp cur, const NTRAHB_Devices device)
{
    busyts[device] = cur+1;
}


// Welcome to my special little hell. :D
void MainRAM_Request(Console* sys, void* req, timestamp now, const bool a9)
{
    BusMainRAM* mr = &sys->BusMR;

    if (a9)
    {
        mr->IsReq9 = true;
        mr->Req9 = *(AHB_Req*)req;
    }
    else
    {
        mr->IsReq7 = true;
        mr->Req7 = *(UnkBus_Req*)req;
    }

    DS_CLAMP(now, <, mr->LastFetchTs)
    NeoSched_AddEvent(sys, now, Evt_MainRAM);
}

bool MainRAM_KillBurst(Console* sys, const timestamp now)
{
    BusMainRAM* mr = &sys->BusMR;

    if (mr->BurstActive)
    {
        timestamp time;
        // stop main ram burst if still running
        if (mr->PrevWrite) time = NTRClock_CvtFrom33(5); // stores: 5 cycle cooldown period
        else               time = NTRClock_CvtFrom33(3); // loads:  3 cycle cooldown period

        if (mr->IsReq9 || mr->IsReq7)
            NeoSched_AddEvent(sys, now + time, Evt_MainRAM);
        mr->BurstLimitTs = timestamp_max;
        mr->LastFetchTs = now + time;
        mr->BurstActive = false;
        mr->CurReq = MainRAM_None;
        return true;
    }
    return false; // burst was already terminated
}

void MainRAM_Run(Console* sys, const timestamp now)
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

    u32 addr;
    AHB_HSIZE size;
    bool nseq;
    bool write;
    bool lock;
    u32 wrval;
    if (grant == MainRAM_None) { MainRAM_KillBurst(sys, now); return; }
    else if (grant == MainRAM_A9)
    {
        AHB_Req* r = &mr->Req9;
        size = r->Size;
        addr = r->Addr;
        nseq = r->Type == HTRANS_NONSEQ;
        write = r->Write;
        lock = r->Lock;
        if (write) wrval = r->WriteVal;
    }
    else
    {
        UnkBus_Req* r = &mr->Req7;
        size = r->Size;
        addr = r->Addr;
        nseq = !r->Seq;
        write = r->Write;
        lock = r->Lock;
        if (write) wrval = r->WriteVal;
    }
    addr = (addr >> size) << size; // make sure addr is aligned for word fetches
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
        mr->BurstLimitTs = now + NTRClock_CvtFrom33(241);
    }
    else MRStepAddr

    timestamp time;

    u32 read;
    if (write)
    {
        read = 0;
        if (size == HSIZE_8)
        {
            if (!nseq) CrashSpectacularly("SEQUENTIAL 8 BIT MAIN RAM WRITE????????????\n");
            MaskedWrite(sys->MainRAM.b16[mr->AddrLatch], wrval, 0xFF << ((addr & 1)*8));
            time = NTRClock_CvtFrom33(4); // takes longer for some reason
            MainRAM_KillBurst(sys, now + NTRClock_CvtFrom33(3)); // CHECKME: it takes less time for it to cooldown, so i assume it does it early somehow??
        }
        else
        {
            sys->MainRAM.b16[mr->AddrLatch] = wrval & 0xFFFF;
            if (size == HSIZE_32)
            {
                MRStepAddr
                sys->MainRAM.b16[mr->AddrLatch] = wrval >> 16;
                time = NTRClock_CvtFrom33(4);
            }
            else time = NTRClock_CvtFrom33(3);
        }
    }
    else // read
    {
        read = sys->MainRAM.b16[mr->AddrLatch];
        if (size < HSIZE_32)
        {
            time = (nseq) ? NTRClock_CvtFrom33(5) : NTRClock_CvtFrom33(1);
            read |= read << 16; // mirror onto both halves of word
        }
        else // 32 bit; do another fetch for high bytes
        {
            time = ((nseq)  ? NTRClock_CvtFrom33(6)
                            : ((now == mr->LastFetchTs) // questionably emulate read prefetching
                                ? NTRClock_CvtFrom33(2)
                                : NTRClock_CvtFrom33(1)));
            MRStepAddr
            read |= sys->MainRAM.b16[mr->AddrLatch] << 16;
        }
    }

    mr->LastFetchTs = now + time;

    if (grant == MainRAM_A9)
    {
        mr->IsReq9 = false;
        AHB9_TransferPostSetup(sys, read, sys->Sched.Times[Evt_AHB9], time + (now - sys->Sched.Times[Evt_AHB9]));
    }
    else
    {
        mr->IsReq7 = false;
        static_assert(false, "oh god i have to do the entire arm7 bus implementation still...\n");
    }

    if (size != HSIZE_8) // this special casing is stupid but i dont wanna fix it
        NeoSched_AddEvent(sys, mr->BurstLimitTs, Evt_MainRAM); // schedule an event to enforce burst limit
}
#undef MRStepAddr

void AHB9_VRAM(Console* sys, u32 addr, const AHB_Req* req, const timestamp now)
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
    u32 rdata;
    timestamp time;
    switch((addr >> 20) & 0xE)
    {
        case 0:  list = VRAM_BGA (sys, addr); break;
        case 2:  list = VRAM_BGB (sys, addr); break;
        case 4:  list = VRAM_OBJA(sys, addr); break;
        case 6:  list = VRAM_OBJB(sys, addr); break;
        default: list = VRAM_LCD (sys, addr); break;
    }

    if (!list)
    {
        // nobody's home
        time = NTRClock_CvtFrom33(1);
        rdata = 0;
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
            rdata = 0;
            u32 wrdata = req->WriteVal;
            // TODO: PPU contention
            time = NTRClock_CvtFrom33(1);
            PPU_Sync(sys, now+time); // TODO: DO THIS BETTER
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
                time += NTRClock_CvtFrom33(1);
                PPU_Sync(sys, now+time); // TODO: DO THIS BETTER
                bank[((addr+2) & (size-1))/2] = wrdata >> 16;
            }
            AddBusContention(sys->AHBBusyTS, now, Dev_VRAM_A + id);
        }
        else
        {
            // TODO: PPU contention
            time = NTRClock_CvtFrom33(1) + BusContention(sys->AHBBusyTS, now, Dev_VRAM_A + id);
            if (req->Size < HSIZE_32)
            {
                rdata = bank[(addr & (size-1))/2];
                rdata |= rdata << 16;
            }
            else
            {
                addr &= ~3;
                rdata = bank[(addr & (size-1))/2];
                time += NTRClock_CvtFrom33(1);
                rdata |= bank[((addr+2) & (size-1))/2] << 16;
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
            if (BusContention(sys->AHBBusyTS, now, Dev_VRAM_A + id))
                contlist |= 1<<id;

            list2 &= (~1)<<id;
        }
        list2 = list;
        // now interate through handling reads/writes
        // TODO: This is probably all wrong and going to need a rewrite to fix a lot of shit. especially writes and ppu interaction
        if (req->Write)
        {
            rdata = 0;
            const u32 wrdata = req->WriteVal;
            time = NTRClock_CvtFrom33((req->Size < HSIZE_32) ? 1 : 2); // dumb
            PPU_Sync(sys, now+time); // no!
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
                AddBusContention(sys->AHBBusyTS, now, Dev_VRAM_A + id); // TODO: busy ones might resolve their writes before unbusy ones...?
                list2 &= (~1)<<id;
            }
        }
        else
        {
            time = NTRClock_CvtFrom33((req->Size < HSIZE_32) ? 1 : 2); // dumb
            if (contlist)
            {
                time += 1; // TODO: this is handled differently than other contention causes; fix that.
                list2 &= contlist; // yes, this is correct(ish)
            }
            rdata = 0;
            while (list2)
            {
                u8 id = stdc_trailing_zeros(list2);
                u16* bank = vram[id].bank;
                size_t size = vram[id].size;
                if (req->Size < HSIZE_32)
                {
                    rdata |= bank[(addr & (size-1))/2];
                    rdata |= rdata << 16;
                }
                else
                {
                    addr &= ~3;
                    rdata |= bank[(addr & (size-1))/2];
                    rdata |= bank[((addr+2) & (size-1))/2] << 16;
                }
                list2 &= (~1)<<id;
            }
        }
    }
    AHB9_TransferPostSetup(sys, rdata, now, time);
}

void AHB9_Read(Console* sys, AHB_Req* req, const timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    if (size > HSIZE_32) CrashSpectacularly("ARM9 AHB READ TOO WIDE: %"PRIu32"\n", width);

    u32 ret;
    timestamp time;
    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        return MainRAM_Request(sys, req, now, true); // defer completion of req to main ram handler

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        // Speculation: like still writing the wram interface? and that's just swallowing the read/write?
        time = NTRClock_CvtFrom33(1) + BusContention(sys->AHBBusyTS, now, Dev_WRAM9);
        switch(sys->WRAMCR)
        {
            case 0: ret = MemoryRead(32, sys->SharedWRAM, addr, SharedWRAM_Size); break;
            case 1: ret = MemoryRead(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2); break;
            case 2: ret = MemoryRead(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2); break;
            case 3: ret = 0; break; // unmapped
            default: unreachable();
        }
        break;

    case 0x04: // Memory Mapped IO
        // checkme: does all of IO have write contention at the same time?
        // checkme: does all of IO have the exact same timings?
        static_assert(false, "this needs a lot of work to improve\n");
        //time = 1 + BusContention(sys->AHBBusyTS, &sys->AHB9.Timestamp, Dev_IO9);
        //ret = IO9_Read(sys, addr & ~3 /* masking like this is probably wrong */, timings);
        break;

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE READ?\n");
            time = NTRClock_CvtFrom33(1);
            ret = 0;
        }
        else
        {
            PPU_Sync(sys, now); // TODO: rework this shit
            time = NTRClock_CvtFrom33(1) + BusContention(sys->AHBBusyTS, now, Dev_Palette);
            if (size >= HSIZE_32)
            {
                PPU_Sync(sys, now + time);
                time += NTRClock_CvtFrom33(1) + BusContention(sys->AHBBusyTS, now + time, Dev_Palette);
                // should technically be two separate reads
                // not sure if that actually matters?
                ret = MemoryRead(32, sys->Palette, addr, Palette_Size);
            }
            else
            {
                ret = MemoryRead(16, sys->Palette, addr, Palette_Size);
                ret |= ret << 16; // fetch is mirrored in both halves
            }
        }
        break;

    case 0x06: // VRAM
        return AHB9_VRAM(sys, addr, req, now);

    case 0x07: // 2D GPU OAM
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED OAM READ?\n");
            time = NTRClock_CvtFrom33(1);
            ret = 0;
        }
        else
        {
            time = NTRClock_CvtFrom33(1) + BusContention(sys->AHBBusyTS, now, Dev_Palette);
            ret = MemoryRead(32, sys->OAM, addr, OAM_Size);
        }
        break;

    case 0x08 ... 0x09: // GBA Game Pak ROM
        if (!sys->ExtMemCR_Shared.GBAPakA7Access) // configured for arm9
        {
            // checkme: what are the odds they kept the prefetcher for some god forsaken reason?
            time = NTRClock_CvtFrom33(1); // TODO
            if (size == HSIZE_32)
            {
                ret = GamePak_ROMRead(&sys->GamePak, addr & ~3);
                ret |= GamePak_ROMRead(&sys->GamePak, (addr & ~3) | 2) << 16;
            }
            else
            {
                ret = GamePak_ROMRead(&sys->GamePak, addr);
                ret |= ret << 16;
            }
        }
        else // unmapped
        {
            time = NTRClock_CvtFrom33(1); // checkme: should this use configured waitstates?
            ret = 0;
        }
        break;

    case 0x0A: // GBA Game Pak SRAM
        // note: 8 bit bus, only supports byte reads, does not ignore low bits of address for larger accesses.
        if (!sys->ExtMemCR_Shared.GBAPakA7Access) // configured for arm9
        {
            if (size != HSIZE_8) LogPrint(LOG_ARM9|LOG_ODD|LOG_PAK, "NTR_AHB9: %"PRIu32" bit read from GBA Game Pak SRAM region, width > 8 bit are weird, probably not correct?\n", width);
            time = NTRClock_CvtFrom33(1); // TODO
            ret = GamePak_SRAMRead(&sys->GamePak, addr);
        }
        else // unmapped
        {
            time = NTRClock_CvtFrom33(1); // checkme: should this use configured waitstates?
            ret = 0; // always returns 0
        }
        ret = ret | (ret << 8) | (ret << 16) | (ret << 24); // byte is mirrored across all bus lanes.
        break;

    case 0xFF: // NDS BIOS
        if ((addr & 0xFFFFF000) == 0xFFFF0000)
        {
            // bios does not have contention, interestingly enough.
            time = NTRClock_CvtFrom33(1);
            ret = MemoryRead(32, sys->NTRBios9, addr, NTRBios9_Size);
            break;
        }
        else
        {
            [[fallthrough]];
        }

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %"PRIu32" bit read from unmapped memory at 0x%08"PRIX32"? Something went wrong?\n", width, addr);
        time = NTRClock_CvtFrom33(1);
        ret = 0; // always reads 0
        break;
    }

    AHB9_TransferPostSetup(sys, ret, now, time);
}

void AHB9_Write(Console* sys, AHB_Req* req, const timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    const u32 mask = MakeWriteMask(addr, size);
    const u32 val = req->WriteVal;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    if (size > HSIZE_32) CrashSpectacularly("ARM9 BUS READ TOO WIDE: %"PRIu32"\n", width);

    timestamp time;
    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        MainRAM_Request(sys, req, now, true);
        return; // defer completion of req to main ram handler

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
        time = NTRClock_CvtFrom33(1);
        AddBusContention(sys->AHBBusyTS, now+time, Dev_WRAM9);
        switch(sys->WRAMCR)
        {
            case 0: MemoryWrite(32, sys->SharedWRAM, addr, SharedWRAM_Size, val, mask); break;
            case 1: MemoryWrite(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2, val, mask); break;
            case 2: MemoryWrite(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2, val, mask); break;
            case 3: break;
            default: unreachable();
        }
        break;

    case 0x04: // Memory Mapped IO
        //time = NTRClock_CvtFrom33(1);
        //AddBusContention(sys->AHBBusyTS, now+time, Dev_WRAM9);
        static_assert(false, "redesign\n");
        //IO9_Write(sys, addr, val, mask);
        break;

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower) || (size == HSIZE_8))
        {
            if (size == HSIZE_8) LogPrint(LOG_ARM9|LOG_ODD, "8 BIT PALETTE WRITE?\n");
            else                 LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE WRITE?\n");
            time = NTRClock_CvtFrom33(1);
            // CHECKME: contention for bytes?
        }
        else
        {
            if (mask & 0x0000FFFF)
            {
                time = NTRClock_CvtFrom33(1);
                PPU_Sync(sys, now+time);
                //BusContention(sys->AHBBusyTS, &sys->AHB9.Timestamp, Dev_Palette);
                //AddBusContention(sys->AHBBusyTS, sys->AHB9.Timestamp, Dev_Palette);

                MemoryWrite(32, sys->Palette, addr, Palette_Size, val, mask & 0x0000FFFF);
            }
            if (mask & 0xFFFF0000)
            {
                time = NTRClock_CvtFrom33(1);
                PPU_Sync(sys, now+time);
                //BusContention(sys->AHBBusyTS, &sys->AHB9.Timestamp, Dev_Palette);
                //AddBusContention(sys->AHBBusyTS, sys->AHB9.Timestamp, Dev_Palette);

                MemoryWrite(32, sys->Palette, addr, Palette_Size, val, mask & 0xFFFF0000);
            }
        }
        break;

    case 0x06: // VRAM
        // TODO: 2d gpu contention timings
        if (size == HSIZE_8)
        {
            LogPrint(LOG_ARM9|LOG_ODD, "ARM9: 8 BIT VRAM WRITE?\n");
            time = NTRClock_CvtFrom33(1);
            // CHECKME: contention for bytes?
        }
        else
        {
            // TODO: update VRAM write handling
            VRAM_ARM9(sys, addr, mask, true, val, true);
        }
        break;

    case 0x07: // 2D GPU OAM
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower) || (width == 8))
        {
            // for some reason oam doesn't support byte writes
            // CHECKME: does it support halfwords?
            if (size == HSIZE_8) LogPrint(LOG_ARM9|LOG_ODD, "8 BIT OAM WRITE?\n");
            else                 LogPrint(LOG_ARM9|LOG_ODD, "DISABLED OAM WRITE?\n");
            time = NTRClock_CvtFrom33(1);
            // CHECKME: contention for bytes?
        }
        else
        {
            time = NTRClock_CvtFrom33(1);
            PPU_Sync(sys, now+time);
            AddBusContention(sys->AHBBusyTS, now+time, Dev_OAM);
            MemoryWrite(32, sys->OAM, addr, OAM_Size, val, mask);
        }
        break;

    case 0x08 ... 0x09: // GBA Game Pak ROM
        if (!sys->ExtMemCR_Shared.GBAPakA7Access) // configured for arm9
        {
            time = NTRClock_CvtFrom33(1); // TODO
            GamePak_ROMWrite(&sys->GamePak, addr, val);
            if (size == HSIZE_32) // TODO: how does this actually work?
                GamePak_ROMWrite(&sys->GamePak, addr+2, val);
        }
        else // unmapped
        {
            time = NTRClock_CvtFrom33(1); // checkme: should this use configured waitstates?
        }
        break;

    case 0x0A: // GBA Game Pak SRAM
        if (!sys->ExtMemCR_Shared.GBAPakA7Access) // configured for arm9
        {
            time = NTRClock_CvtFrom33(1); // TODO
            GamePak_SRAMWrite(&sys->GamePak, addr, ROR32(val, 8*addr)); // CHECKME
        }
        else // unmapped
        {
            time = NTRClock_CvtFrom33(1); // checkme: should this use configured waitstates?
        }
        break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %"PRIu32" bit write to unmapped memory at 0x%08"PRIX32"? Something went wrong?\n", width, addr);
        time = NTRClock_CvtFrom33(1);
        break;
    }

    AHB9_TransferPostSetup(sys, 0, now, time);
}

void AHB9_Idle(Console* sys, const timestamp now)
{
    // no access performed; signal to kill bursts
    if (sys->BusMR.CurReq == MainRAM_A9)
        MainRAM_KillBurst(sys, now);

    // TODO: kill gba rom/ram chipsel
}

void AHB9_Busy(Console* sys, const timestamp now)
{
    if (sys->BusMR.CurReq == MainRAM_A9)
        MainRAM_KillBurst(sys, now);
}

void AHB9_BusReq(Console* sys, AHB_Req* req, timestamp now)
{
    AHB* ahb = &sys->AHB9;

    ahb->RequestBitfield |= (1<<(req->Manager));
    ahb->Reqs[req->Manager] = *req;

    if (!ahb->LockSched) NeoSched_AddEventIfEarlier(sys, now, Evt_AHB9);
}

void AHB9_TransferPostSetup(Console* sys, u32 rdata, timestamp prev, timestamp len)
{
    AHB* ahb = &sys->AHB9;
    ahb->ReadData = rdata;
    ahb->PrevTs = prev;
    ahb->FetchLen = len;
    NeoSched_AddEvent(sys, prev+len, Evt_AHB9);
}

void AHB9_TransferPost(Console* sys)
{
    AHB* ahb = &sys->AHB9;
    u32 rdata = ahb->ReadData;
    timestamp prev = ahb->PrevTs;
    timestamp len = ahb->FetchLen;
    // ahb pipeline steps once every HREADY

    // apply waitstate delays
    if (len > NTRClock_CvtFrom33(1))
    {
        for (s32 i = 0; i < 4; i++)
        {
            ahb->PipelineExitTime[i] += (len-NTRClock_CvtFrom33(1));
        }
    }

    // arbitrate new reqs
    if (ahb->RequestBitfield)
    {
        AHB9_HMANAGER manager;
        // handle locked transfers
        if (ahb->HLock == MAN9_MAX)
        {
            manager = stdc_trailing_zeros(ahb->RequestBitfield);
        }
        else
        {
            // locked, manager stays the original
            manager = ahb->HLock;
            // make sure it's actually trying to do a transfer
            if (!(ahb->RequestBitfield & (1<<manager))) goto nvm;
        }

        // put entry into fifo
        ahb->PipelineFIFO[ahb->FIFOFillPtr] = ahb->Reqs[manager];
        ahb->PipelineExitTime[ahb->FIFOFillPtr] = prev+len + NTRClock_CvtFrom33(3 /* checkme */);

        ahb->RequestBitfield &= ~(1<<manager); // clear req list
        // update lock flag
        ahb->HLock = ((ahb->PipelineFIFO[ahb->FIFOFillPtr].Lock) ? manager : MAN9_MAX);

        // step fill ptr
        ahb->FIFOFillPtr = (ahb->FIFOFillPtr + 1) % countof(ahb->PipelineFIFO);
        ahb->FIFOEmpty = false;
    }
    nvm:

    // schedule next event
    const timestamp fin = prev + len;
    timestamp new;
    // check if something needs arbitration arbitrated
    if (ahb->HLock ? (ahb->RequestBitfield & (1<<ahb->HLock)) : ahb->RequestBitfield)
    {
        // step pipeline 1 cycle
        new = fin + NTRClock_CvtFrom33(1);
    }
    else if (!ahb->FIFOEmpty)
    {
        // if something is in the pipeline wait for it
        new = ahb->PipelineExitTime[ahb->FIFODrainPtr];
    }
    else
    {
        // nothing to do; ahb go nini
        goto noresched;
    }
    NeoSched_AddEvent(sys, new, Evt_AHB9);
    noresched:
    ahb->LockSched = false;

    static_assert(false, "ADD CALLBACKS\n");
    // req callback
}

void AHB9_BusRun(Console* sys, timestamp now)
{
    AHB* ahb = &sys->AHB9;


    if (ahb->LockSched)
    {
        AHB9_TransferPost(sys);
    }
    else ahb->LockSched = true;

    // step pipeline
    if (!ahb->FIFOEmpty)
    {
        AHB_Req* req = &ahb->PipelineFIFO[ahb->FIFODrainPtr];
        if ((ahb->PipelineExitTime[ahb->FIFODrainPtr] <= now))
        {
            ahb->FIFODrainPtr = (ahb->FIFODrainPtr + 1) % countof(ahb->PipelineFIFO);

            if (ahb->FIFODrainPtr == ahb->FIFOFillPtr)
                ahb->FIFOEmpty = true;

            if (req->Type >= HTRANS_NONSEQ)
            {
                // its time; begin transfer!
                if (req->Write) AHB9_Write(sys, req, now);
                else            AHB9_Read(sys, req, now);
            }
            else
            {
                // explicit idle/busy transfer; 1 cycle
                static_assert(false, "idle and busy transfers likely do different things for some regions\n");
            }
        }
        else {} // not time yet; assume 1 cycle idle transfer
    }
    else {} // nothing running; assume 1 cycle idle transfer

    AHB9_TransferPostSetup(sys, 0, now, NTRClock_CvtFrom33(1));
}

u32 AHB7_Read(Console* sys, timestamp* ts, u32 addr, const AHB_HSIZE size, const bool atomic, const bool hold, bool* seq, const bool timings, const u32 a7pc)
{
    if (size > HSIZE_32) CrashSpectacularly("ARM7 BUS READ TOO WIDE: %"PRIu32"\n", 8<<size);
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?
    if (timings)
    {
        if (sys->AHB7.Timestamp < *ts)
            sys->AHB7.Timestamp = *ts;

        if (sys->AHB7.HoldingMainRAM)
        {
            sys->AHB7.HoldingMainRAM = false;
            if (((addr>>24) != 0x02) || !hold) Bus_MainRAM_ReleaseHold(sys, &sys->AHB7);
        }
    }

    u32 ret;
    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x000: // ARM7 BIOS
        if (addr < 0x4000)
        {
            if (timings)
            {
                // CHECKME: does bios7 write contention work weirdly with bios prot?
                BusContention(sys->AHBBusyTS, &sys->AHB7.Timestamp, Dev_Bios7);
                Timing32(&sys->AHB7);
            }
            // a7 bios reads have protection
            // TODO: contemplate exact bios protection mechanism
            if ((a7pc >= 0x4000) || ((addr < sys->Bios7Prot) && (a7pc >= sys->Bios7Prot)))
            {
                ret = 0xFFFFFFFF;
                LogPrint(LOG_ARM7|LOG_ODD, "Protected Bios7 Read? Addr: %08X PC: %08X\n", addr, a7pc);
            }
            else ret = MemoryRead(32, sys->NTRBios7, addr, NTRBios7_Size);
            break;
        }
        else
        {
            [[fallthrough]];
        }

    default: // Unmapped Device;
        if (timings) LogPrint(LOG_ODD|LOG_ARM7, "NTR_AHB7: %i bit read from unmapped memory at 0x%08X? Something went wrong?\n", (8<<size), addr);
        if (timings)
        {
            Timing32(&sys->AHB7);
        }
        ret = 0; // always reads 0
        break;

    case 0x020 ... 0x028: // Main RAM
        ret = Bus_MainRAM_Read(sys, &sys->AHB7, false, addr, size, atomic, hold, seq, timings);
        break;

    case 0x030: // Shared WRAM
        if (timings)
        {
            BusContention(sys->AHBBusyTS, &sys->AHB7.Timestamp, Dev_WRAM7);
            Timing32(&sys->AHB7);
            Scheduler_Sync(sys, sys->AHB7.Timestamp, Sync_Normal7);
        }
        switch(sys->WRAMCR)
        {
            case 0:
                ret = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size); break;
            case 1:
                ret = MemoryRead(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2); break;
            case 2:
                ret = MemoryRead(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2); break;
            case 3:
                ret = MemoryRead(32, sys->SharedWRAM, addr, SharedWRAM_Size); break;
            default: unreachable();
        }
        break;

    case 0x038: // ARM7 WRAM
        if (timings)
        {
            BusContention(sys->AHBBusyTS, &sys->AHB7.Timestamp, Dev_WRAM7);
            Timing32(&sys->AHB7);
        }
        ret = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size);
        break;

    case 0x040: // Memory Mapped IO
        if (timings)
        {
            BusContention(sys->AHBBusyTS, &sys->AHB7.Timestamp, Dev_IO7); // checkme: does all of IO have write contention at the same time?
            Timing32(&sys->AHB7); // checkme: does all of IO have the exact same timings?
            ret = IO7_Read(sys, addr & ~3 /* masking like this is probably wrong */, timings);
        }
        else ret = 0; // todo: fix this
        break;
    case 0x048: // WiFi
        ret = WiFi_Read(sys, ts, addr, size, timings);
        break;

    case 0x060 ... 0x068: // VRAM
        // TODO: update VRAM read handling
        u32 mask;
        if (size == HSIZE_32) mask = u32_max;
        if (size == HSIZE_16) mask = ROL32(u16_max, (addr & 2) * 8);
        if (size == HSIZE_8 ) mask = ROL32(u8_max , (addr & 3) * 8);
        ret = VRAM_ARM7(sys, addr, mask, false, 0, timings);
        break;

    case 0x080 ... 0x098: // GBA Game Pak ROM
        if (sys->ExtMemCR_Shared.GBAPakAccess) // configured for arm7
        {
            // checkme: what are the odds they kept the prefetcher for some god forsaken reason?
            if (timings) Timing32(&sys->AHB7); // TODO
            if (size == HSIZE_32)
            {
                ret = GamePak_ROMRead(&sys->GamePak, addr & ~3);
                ret |= GamePak_ROMRead(&sys->GamePak, (addr & ~3) | 2) << 16;
            }
            else
            {
                ret = GamePak_ROMRead(&sys->GamePak, addr);
                ret |= ret << 16;
            }
        }
        else // unmapped
        {
            if (timings) Timing32(&sys->AHB7); // checkme: should this use configured waitstates?
            ret = 0;
        }
        break;

    case 0x0A0 ... 0x0A8: // GBA Game Pak SRAM
        // note: 8 bit bus, only supports byte reads, does not ignore low bits of address for larger accesses.
        if (sys->ExtMemCR_Shared.GBAPakAccess) // configured for arm7
        {
            if (timings && (size != HSIZE_8)) LogPrint(LOG_ARM7|LOG_ODD|LOG_PAK, "NTR_AHB7: %i bit read from GBA Game Pak SRAM region, width > 8 bit are weird, probably not correct?\n", (8<<size));
            if (timings) Timing32(&sys->AHB7); // TODO
            ret = GamePak_SRAMRead(&sys->GamePak, addr);
        }
        else // unmapped
        {
            if (timings) Timing32(&sys->AHB7); // checkme: should this use configured waitstates?
            ret = 0; // always returns 0
        }
        ret = ret | (ret << 8)| (ret << 16) | (ret << 24); // byte is mirrored across all bus lanes.
        break;
    }

    if (timings)
    {
        *ts = sys->AHB7.Timestamp;
    }
    return ret;
}

void AHB7_Write(Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq, const bool timings, const u32 a7pc)
{
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?
    if (timings)
    {
        if (sys->AHB7.Timestamp < *ts)
            sys->AHB7.Timestamp = *ts;
    }

    addr &= ~3; // 4 byte aligned value used to simplify write logic.

    const unsigned width = stdc_count_ones(mask);

    switch(addr >> 20 & 0xFF8) // check most signficant byte (and msb of second byte)
    {
    case 0x000: // ARM7 BIOS
        if (timings && (addr < 0x4000))
        {
            // CHECKME: does bios7 write contention work weirdly with bios prot?
            Timing32(&sys->AHB7);
            AddBusContention(sys->AHBBusyTS, sys->AHB7.Timestamp, Dev_Bios7);
        }
        break;

    case 0x020 ... 0x028: // Main RAM
        Bus_MainRAM_Write(sys, &sys->AHB7, false, addr, val, mask, atomic, seq, timings);
        break;

    case 0x030: // Shared WRAM
        if (timings)
        {
            Timing32(&sys->AHB7);
            AddBusContention(sys->AHBBusyTS, sys->AHB7.Timestamp, Dev_WRAM7);
            Scheduler_Sync(sys, sys->AHB7.Timestamp, Sync_Normal7);
        }
        switch(sys->WRAMCR)
        {
            case 0:
                MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, val, mask); break;
            case 1:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)))/4], val, mask); break;
            case 2:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size/2)-1)) + (SharedWRAM_Size/2))/4], val, mask); break;
            case 3:
                MaskedWrite(sys->SharedWRAM.b32[((addr & ((SharedWRAM_Size)-1)))/4], val, mask); break;
            default: unreachable();
        }
        break;

    case 0x038: // ARM7 WRAM
        if (timings)
        {
            Timing32(&sys->AHB7);
            AddBusContention(sys->AHBBusyTS, sys->AHB7.Timestamp, Dev_WRAM7);
        }
        MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, val, mask);
        break;

    case 0x040: // Memory Mapped IO
        if (timings)
        {
            Timing32(&sys->AHB7); // checkme: does all of IO have the exact same timings?
            AddBusContention(sys->AHBBusyTS, sys->AHB7.Timestamp, Dev_IO7);
        }
        IO7_Write(sys, addr, val, mask, a7pc);
        break;
    case 0x048: // WiFi
        WiFi_Write(sys, ts, addr, val, mask, timings);
        break;

    case 0x060 ... 0x068: // VRAM
        // TODO: update VRAM write handling
        VRAM_ARM7(sys, addr, mask, true, val, timings);
        break;

    case 0x080 ... 0x098: // GBA Game Pak ROM
        if (sys->ExtMemCR_Shared.GBAPakAccess) // configured for arm7
        {
            if (timings) Timing32(&sys->AHB7);
            GamePak_ROMWrite(&sys->GamePak, addr, val);
            if (width == 32) // TODO: how does this actually work?
                GamePak_ROMWrite(&sys->GamePak, addr+2, val >> 16);
        }
        else // unmapped
        {
            if (timings) Timing32(&sys->AHB7); // checkme: should this use configured waitstates?
        }
        break;

    case 0x0A0 ... 0x0A8: // GBA Game Pak SRAM
        if (sys->ExtMemCR_Shared.GBAPakAccess) // configured for arm7
        {
            if (timings) Timing32(&sys->AHB7);
            u32 fixedaddr = addr | (stdc_trailing_zeros(mask) / 8); // NOT ACCURATE: TODO FIX
            u8 fixedval = ROR32(val, stdc_trailing_zeros(mask));
            GamePak_SRAMWrite(&sys->GamePak, fixedaddr, fixedval);
        }
        else // unmapped
        {
            if (timings) Timing32(&sys->AHB7); // checkme: should this use configured waitstates?
        }
        break;

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM7,"NTR_AHB7: %i bit write to unmapped memory at 0x%08X? Something went wrong?\n", width, addr);
        if (timings) Timing32(&sys->AHB7);
        break;
    }

    if (timings)
    {
        *ts = sys->AHB7.Timestamp;
    }
}
