#include <assert.h>
#include <stddef.h>
#include "bus.h"
#include "core/arm/arm7/arm.h"
#include "core/arm/arm9/arm.h"
#include "core/utils.h"
#include "core/io/dma.h"
#include "core/console.h"
#include "core/carts/gamepak.h"
#include "core/video/video.h"
#include "core/scheduler.h"
#include "vram.h"


bool Bus_DebugBreak(Console* sys, timestamp now, Bus_Breakpoint bkptlist[const Bus_DebugMaxWatch], u64 bkptnum, u32 addr, u8 size, bool write, u8 man, u32 matchdata)
{
    for (u64 i = 0; i < bkptnum; i++)
    {
        if (write ? bkptlist[i].MustRead : bkptlist[i].MustWrite)
            continue;
        if (((u32)bkptlist[i].MaskForMatch & (u32)bkptlist[i].DataToMatch) != ((u32)bkptlist[i].MaskForMatch & matchdata))
            continue;
        if (!(bkptlist[i].ManMask & ((u32)1<<man)))
            continue;
        if ((u32)bkptlist[i].AddrMin > addr)
            continue;
        if ((u32)bkptlist[i].AddrMax < addr)
            continue;
        if (!(bkptlist[i].WidthMask & ((u32)1<<size)))
            continue;

        Sched_AddEvent(sys, now, Evt_DebugBreak);
        return true;
    }
    return false;
}

void Bus9_Init(BusImpl* bus)
{
    bus->PipeCycles = DSClk33(3);
    bus->HLockGeneric = MAN_NONE; // todo: put in a reset handler
    bus->CmpMan = MAN_NONE;
    bus->NoPrev = true;
    bus->FIFOEmpty = true;
}

void Bus7_Init(BusImpl* bus)
{
    // TODO: RE-ENABLE
    bus->PipeCycles = DSClk33(0);
    bus->HLockGeneric = MAN_NONE; // todo: put in a reset handler
    bus->CmpMan = MAN_NONE;
    bus->NoPrev = true;
    bus->FIFOEmpty = true;
}

void MainRAM_Init(Console* sys, NTRFCRAM fcramsize)
{
    sys->BusMR.BurstLimitTs = timestamp_max;
    switch(fcramsize)
    {
    case NTRFCRAM_4MiB:  sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(4)-1)); break;
    case NTRFCRAM_8MiB:  sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(8)-1)); break;
    case NTRFCRAM_16MiB: sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(16)-1)); break;
    case NTRFCRAM_32MiB: sys->BusMR.AddrSubmMask = (sys->BusMR.AddrLatchMask = (MiB(32)-1)); break;
    }
    sys->BusMR.AddrLatchMask >>= 1; // latched addresses are halfword addrs, not byte
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
    sys->AHBBusyTS[device] = cur+DSClk33(1)+DSClk33(1);
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

        if (mr->IsReq9 || mr->IsReq7)
        {
            Sched_AddEvent(sys, now, Evt_MainRAM);
        }
        mr->BurstLimitTs = timestamp_max;
        mr->LastFetchTs = now;
        mr->BurstActive = false;
        mr->CurReq = MainRAM_None;
        return true;
    }
    return false; // burst was already terminated
}

void MainRAM_TestKillBurst(Console* sys, timestamp now, bool a9)
{
    BusMainRAM* mr = &sys->BusMR;
    if (a9) { if (mr->CurReq != MainRAM_A9) return; }
    else    { if (mr->CurReq != MainRAM_A7) return; }
    MainRAM_KillBurst(sys, now+DSClk33(1));
}

void MainRAM_Run(Console* sys, timestamp now)
{
    BusMainRAM* mr = &sys->BusMR;
    MainRAM_Buses grant = MainRAM_None;

    if (now > mr->BurstLimitTs && MainRAM_KillBurst(sys, now)) return;

    if (mr->Locked != MainRAM_None) // main ram interface respects atomic lock signals
    {
        // make sure there's an actual req happening first, just in case; CHECKME: is this check useful?
        if (mr->Locked == MainRAM_A9)
        {
            if (mr->IsReq9) grant = MainRAM_A9;
            //else LogPrint(LOG_ARM9|LOG_FCRAM, "ARM9 Atomic access to fcram but no req?\n"); checkme: i dont think this is actually an issue?
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

    BusReq* r = ((grant == MainRAM_A9) ? (&sys->Bus9.PipeFIFO[sys->Bus9.ReqActivePtr])
                                       : (&sys->Bus7.PipeFIFO[sys->Bus7.ReqActivePtr]));
    AHB_HSIZE size = r->Size;
    u32 addr = (r->Addr >> size) << size; // make sure addr is aligned for word fetches
    bool nseq = r->Type == HTRANS_NONSEQ;
    bool write = r->Write;
    bool lock = r->Lock;
    u8 man = r->Man;
    u32 wrdata; if (write) wrdata = r->WrData;

    if (write && (addr & 2)) wrdata >>= 16;

    if (!mr->BurstActive) nseq = true;
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

    mr->BurstActive = true;

    if (lock) mr->Locked = grant;
    else mr->Locked = false;

#define MRStepAddr mr->AddrLatch = (mr->AddrLatch + 1) & mr->AddrLatchMask;

    u32 fauxaddr = (addr & mr->AddrSubmMask) >> 1;
    if (nseq)
    {
        mr->AddrLatch = (addr & mr->AddrSubmMask) >> 1;
        mr->WeirdStart = ((addr & 0x1E) >= 0x1A);
        mr->BurstLimitTs = now + DSClk33(241) + DSClk33(1);
    }
    else MRStepAddr

    //mr->AddrLatch = (addr & mr->AddrSubmMask) >> 1;

    if ((addr & 0xFF000000) != 0x02000000) printf("BAD ADDRESS????\n");
    if ((r->Type < HTRANS_NONSEQ)) printf("BAD TRANSFER TYPE??\n");
    if ((mr->AddrLatch != fauxaddr) || ((addr & 0xFF000000) != 0x02000000))
    {
        LogPrint(LOG_FCRAM, "MR ADDR MISMATCH: %08X %08X %i %i %i %i\n", mr->AddrLatch << 1, addr, grant == MainRAM_A9, man, r->CB, r->Type);
#ifdef MRTURBOLOG
        BusReq* l;
        for (size_t i = 0; i < countof(mr->REQLOG); i++)
        {
            l = &mr->REQLOG[(mr->REQLOGPTR + i) % countof(mr->REQLOG)];
            printf("%02zu: a: %08"PRIX32" d: %08"PRIX32" w:%i l:%i m:%"PRIu8" p:%01"PRIX8" s:%01"PRIX8" t:%01"PRIX8" c:%02"PRIu8"\n", i, l->Addr, l->WrData, l->Write, l->Lock, l->Man, *(u8*)&l->Prot, l->Size, l->Type, l->CB);
        }
        l = r;
        printf("%02zu: a: %08"PRIX32" d: %08"PRIX32" w:%i l:%i m:%"PRIu8" p:%01"PRIX8" s:%01"PRIX8" t:%01"PRIX8" c:%02"PRIu8"\n", (size_t)32, l->Addr, l->WrData, l->Write, l->Lock, l->Man, *(u8*)&l->Prot, l->Size, l->Type, l->CB);
#endif
    }
    u32 rdata;
    if (write)
    {
        rdata = 0;
        if (size == HSIZE_8)
        {
            if (!nseq) CrashSpectacularly("SEQUENTIAL 8 BIT MAIN RAM WRITE????????????\n");
            MaskedWrite(sys->MainRAM.b16[mr->AddrLatch], wrdata, 0xFF << ((addr & 1)*8));
            now += DSClk33(3); // takes longer for some reason
            //MainRAM_KillBurst(sys, now - DSClk33(1)); // CHECKME: it takes less time for it to cooldown, so i assume it does it early somehow??
        }
        else
        {
            sys->MainRAM.b16[mr->AddrLatch] = wrdata & 0xFFFF;
            if (size == HSIZE_32)
            {
                MRStepAddr
                sys->MainRAM.b16[mr->AddrLatch] = wrdata >> 16;
                now += nseq ? DSClk33(3) : DSClk33(1);
            }
            else now += nseq ? DSClk33(2) : DSClk33(0);
        }
    }
    else // read
    {
        rdata = sys->MainRAM.b16[mr->AddrLatch];
        if (size < HSIZE_32)
        {
            now += (nseq) ? DSClk33(4) : DSClk33(0);
            rdata |= rdata << 16; // mirror onto both halves of word
        }
        else // 32 bit; do another fetch for high bytes
        {
            now += ((nseq)  ? DSClk33(5)
                            : ((now <= mr->LastFetchTs) // questionably emulate read prefetching
                                ? DSClk33(1)
                                : DSClk33(0)));
            MRStepAddr
            rdata |= sys->MainRAM.b16[mr->AddrLatch] << 16;
        }
    }

    mr->LastFetchTs = now + DSClk33(1);

#ifdef MRTURBOLOG
    mr->REQLOG[mr->REQLOGPTR++] = ((grant == MainRAM_A9) ? (sys->Bus9.PipeFIFO[sys->Bus9.ReqActivePtr])
                                                         : (sys->Bus7.PipeFIFO[sys->Bus7.ReqActivePtr]));\
    mr->REQLOGPTR %= countof(mr->REQLOG);
#endif

    Bus_TransferPostSetup(sys, rdata, !write, now, false, (grant == MainRAM_A9));

    if (grant == MainRAM_A9) mr->IsReq9 = false;
    else                     mr->IsReq7 = false;

    return Sched_AddEvent(sys, mr->BurstLimitTs, Evt_MainRAM);
}
#undef MRStepAddr

u32 Bus_VRAMDebugRead(Console* sys, u32 addr, const bool a9)
{
    const struct
    {
        u32* bank;
        size_t size;
    } vram[VRAMID_MAX] =
    {
        {sys->VRAM_A.b32, VRAM_A_Size},
        {sys->VRAM_B.b32, VRAM_B_Size},
        {sys->VRAM_C.b32, VRAM_C_Size},
        {sys->VRAM_D.b32, VRAM_D_Size},
        {sys->VRAM_E.b32, VRAM_E_Size},
        {sys->VRAM_F.b32, VRAM_F_Size},
        {sys->VRAM_G.b32, VRAM_G_Size},
        {sys->VRAM_H.b32, VRAM_H_Size},
        {sys->VRAM_I.b32, VRAM_I_Size}
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

    if (!list) return 0;
    else if (stdc_count_ones(list) == 1)
    {
        u8 id = stdc_trailing_zeros(list);
        u32* bank = vram[id].bank;
        size_t size = vram[id].size;
        return bank[(addr & (size-1))/4];
    }
    else // overlap; slow handler
    {
        u32 rdata = 0;
        while (list)
        {
            u8 id = stdc_trailing_zeros(list);
            list &= (~1)<<id;
            u32* bank = vram[id].bank;
            size_t size = vram[id].size;
            rdata |= bank[(addr & (size-1))/4];
        }
        return rdata;
    }
}

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
        if (!req->Write) *rdata = 0;
    }
    else if (stdc_count_ones(list) == 1)
    {
        // 1 region, use simpler logic
        // TODO: just use lut for this case?
        u8 id = stdc_trailing_zeros(list);
        u16* bank = vram[id].bank;
        size_t size = vram[id].size;
        if (req->Write)
        {
            u32 wrdata = req->WrData;
            // TODO: PPU contention
            PPU_Sync(sys, *now); // TODO: DO THIS BETTER
            if (req->Size < HSIZE_32)
            {
                wrdata = ROR32(wrdata, (addr & 2) * 8);
                if (req->Size == HSIZE_8)
                {
                    MaskedWrite(bank[(addr & (size-1))/2], wrdata, 0xFF << ((addr&1)*8));
                }
                else bank[(addr & (size-1))/2] = wrdata;
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
            *now += BusContention(sys, *now, Dev_VRAM_A + id);
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
            const u32 wrdata = req->WrData;
            *now += DSClk33((req->Size < HSIZE_32) ? 0 : 1); // dumb
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
                    else bank[(addr & (size-1))/2] = tmpwrdata;
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
            *now += DSClk33((req->Size < HSIZE_32) ? 0 : 1); // dumb
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
        *now += DSClk33(0); // TODO
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
        *now += DSClk33(0); // checkme: should this use configured waitstates?
        *rdata = 0;
    }
}
void GamePakBus_ROMWrite(Console* sys, const u32 wrdata, timestamp* now, const u32 addr, const AHB_HSIZE size, const bool a9)
{
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        *now += DSClk33(0); // TODO
        GamePak_ROMWrite(&sys->GamePak, addr, wrdata);
        if (size == HSIZE_32) // TODO: how does this actually work?
            GamePak_ROMWrite(&sys->GamePak, addr+2, wrdata);
    }
    else *now += DSClk33(0); // unmapped; checkme: should this use configured waitstates?
}

void GamePakBus_RAMRead(Console* sys, u32* rdata, timestamp* now, const u32 addr, const AHB_HSIZE size, const bool a9)
{
    // note: 8 bit bus, only supports byte reads, does not ignore low bits of address for larger accesses.
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        if (size != HSIZE_8) LogPrint((a9 ? LOG_ARM9 : LOG_ARM7)|LOG_ODD|LOG_PAK, "NTR_Bus%"PRIu8": %"PRIu32" bit read from GBA Game Pak SRAM, width > 8 bit are weird, probably not correct?\n", 7+(a9*2), 8<<size);
        *now += DSClk33(0); // TODO
        *rdata = GamePak_SRAMRead(&sys->GamePak, addr);
    }
    else // unmapped
    {
        *now += DSClk33(0); // checkme: should this use configured waitstates?
        *rdata = 0; // always returns 0
    }
    *rdata = *rdata | (*rdata << 8) | (*rdata << 16) | (*rdata << 24); // byte is mirrored across all bus lanes.
}
void GamePakBus_RAMWrite(Console* sys, const u32 wrdata, timestamp* now, const u32 addr, const AHB_HSIZE size [[maybe_unused]], const bool a9)
{
    if (a9 != sys->ExtMemCR_Shared.GBAPakA7Access)
    {
        //if (size != HSIZE_8) LogPrint((a9 ? LOG_ARM9 : LOG_ARM7)|LOG_ODD|LOG_PAK, "NTR_Bus%"PRIu8": %"PRIu32" bit write to GBA Game Pak SRAM, width > 8 bit are weird, probably not correct?\n", 7+(a9*2), 8<<size);
        *now += DSClk33(0); // TODO
        GamePak_SRAMWrite(&sys->GamePak, addr, ROR32(wrdata, 8*addr) /* select proper byte lanes */); // CHECKME
    }
    else *now += DSClk33(0); // unmapped; checkme: should this use configured waitstates?
}

u32 Bus9_DebugRead(Console* sys, u32 addr)
{
    switch(addr >> 24)
    {
    case 0x02: return MemoryRead(32, sys->MainRAM, addr, sys->BusMR.AddrSubmMask);
    case 0x03:
        switch(sys->WRAMCR)
        {
            case 0: return MemoryRead(32, sys->SharedWRAM,   addr, SharedWRAM_Size  );
            case 1: return MemoryRead(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2);
            case 2: return MemoryRead(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2);
            case 3: return 0; break; // unmapped
            default: unreachable();
        }
    case 0x04: return 0; // TODO
    case 0x05: return MemoryRead(32, sys->Palette, addr, Palette_Size);
    case 0x06: return Bus_VRAMDebugRead(sys, addr, true);
    case 0x07: return MemoryRead(32, sys->OAM, addr, OAM_Size);
    case 0x08 ... 0x09: return 0; // TODO
    case 0x0A: return 0; // TODO
    case 0xFF: if ((addr & 0xFFFFF000) == 0xFFFF0000) return MemoryRead(32, sys->NTRBios9, addr, NTRBios9_Size);
               else { [[fallthrough]]; }
    default: return 0;
    }
}

u32 Bus7_DebugRead(Console* sys, u32 addr)
{
    switch((addr>>20) & 0xFF8)
    {
    case 0x000: if (addr < 0x4000) return MemoryRead(32, sys->NTRBios7, addr, NTRBios7_Size);
                else { [[fallthrough]]; }
    default: return 0;
    case 0x020 ... 0x028: return MemoryRead(32, sys->MainRAM, addr, sys->BusMR.AddrSubmMask);
    case 0x030:
        switch(sys->WRAMCR)
        {
        case 0: return MemoryRead(32, sys->ARM7WRAM,     addr, ARM7WRAM_Size    );
        case 1: return MemoryRead(32, sys->SharedWRAMLo, addr, SharedWRAM_Size/2);
        case 2: return MemoryRead(32, sys->SharedWRAMHi, addr, SharedWRAM_Size/2);
        case 3: return MemoryRead(32, sys->SharedWRAM,   addr, SharedWRAM_Size  );
        default: unreachable();
        }
    case 0x038: return MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size);
    case 0x040: return 0; // TODO
    case 0x048: return 0; // TODO
    case 0x060 ... 0x068: return Bus_VRAMDebugRead(sys, addr, false);
    case 0x080 ... 0x098: return 0; // TODO
    case 0x0A0 ... 0x0A8: return 0; // TODO
    }
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
        now += BusContention(sys, now, Dev_WRAM9);
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
        return Sched_AddEvent(sys, now + BusContention(sys, now, Dev_IO9), Evt_IO9); // io is in a separate event for simplicity's sake

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower))
        {
            LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE READ?\n");
            rdata = 0;
        }
        else
        {
            PPU_Sync(sys, now); // TODO: rework this shit
            now += BusContention(sys, now, Dev_Palette);
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
            rdata = 0;
        }
        else
        {
            now += BusContention(sys, now, Dev_Palette);
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
            rdata = MemoryRead(32, sys->NTRBios9, addr, NTRBios9_Size);
            break;
        }
        else { [[fallthrough]]; }

    default: // Unmapped Device;
        LogPrint(LOG_ODD|LOG_ARM9,"NTR_AHB9: %"PRIu32" bit read from unmapped memory at 0x%08"PRIX32"? Something went wrong?\n", width, addr);
        rdata = 0; // always reads 0
        break;
    }

    Bus_TransferPostSetup(sys, rdata, true, now, false, true);
}

void Bus9_Write(Console* sys, BusReq* req, timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    const u32 mask = MakeWriteMask(addr, size);

    // disgusting hack
    if (req->Man9 >= MAN9_DMA0 && req->Man9 <= MAN9_NDMA3)
        req->WrData = sys->DMA9[req->Man9-MAN9_DMA0].RData;

    const u32 wrdata = req->WrData;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    if (size > HSIZE_32) CrashSpectacularly("ARM9 BUS READ TOO WIDE: %"PRIu32"\n", width);

    switch(addr >> 24) // check most signficant byte
    {
    case 0x02: // Main RAM
        return MainRAM_Request(sys, now, true); // defer completion of req to main ram handler

    case 0x03: // Shared WRAM
        // NOTE: it seems to still have write contention even if unmapped?
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
        return Sched_AddEvent(sys, now, Evt_IO9); // io is in a separate event for simplicity's sake

    case 0x05: // 2D GPU Palette
        // TODO: 2d gpu contention timings
        if (!((addr & 0x400) ? sys->PowerCR9.PPUBPower : sys->PowerCR9.PPUAPower) || (size == HSIZE_8))
        {
            if (size == HSIZE_8) LogPrint(LOG_ARM9|LOG_ODD, "8 BIT PALETTE WRITE?\n");
            else                 LogPrint(LOG_ARM9|LOG_ODD, "DISABLED PALETTE WRITE?\n");
            // CHECKME: contention for bytes?
        }
        else
        {
            if (mask & 0x0000FFFF)
            {
                PPU_Sync(sys, now);
                //BusContention(sys->AHBBusyTS, &sys->AHB9.Timestamp, Dev_Palette);
                //AddBusContention(sys->AHBBusyTS, sys->AHB9.Timestamp, Dev_Palette);

                MemoryWrite(32, sys->Palette, addr, Palette_Size, wrdata, mask & 0x0000FFFF);
            }
            if (mask & 0xFFFF0000)
            {
                if (size >= HSIZE_32) now += DSClk33(1);
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
            // CHECKME: contention for bytes?
        }
        else
        {
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
        break;
    }

    Bus_TransferPostSetup(sys, 0, false, now, false, true);
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
        else sys->Bios7ProtCur = 0;
    }

    u32 rdata;
    switch((addr>>20) & 0xFF8) // check most signficant 9 bits
    {
    case 0x000: // ARM7 BIOS
        if (addr < 0x4000)
        {
            // CHECKME: does bios7 write contention work weirdly with bios prot?
            // CHECKME: does bios7 write contention exist?
            //BusContention(sys->AHBBusyTS, &sys->AHB7.Timestamp, Dev_Bios7);

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
        rdata = 0; // always reads 0
        break;

    case 0x020 ... 0x028: // Main RAM
        return MainRAM_Request(sys, now, false); // defer completion of req to main ram handler

    case 0x030: // Shared WRAM
        now += BusContention(sys, now, Dev_WRAM7);
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
        now += BusContention(sys, now, Dev_WRAM7 /* checkme: i think this is wrong actually...? */);
        rdata = MemoryRead(32, sys->ARM7WRAM, addr, ARM7WRAM_Size);
        break;

    case 0x040: // Memory Mapped IO
        return Sched_AddEvent(sys, now + BusContention(sys, now, Dev_IO7), Evt_IO7); // io is in a separate event for simplicity's sake

    case 0x048: // WiFi
        WiFi_Read(sys, &rdata, &now, addr, size); break;

    case 0x060 ... 0x068: // VRAM
        Bus_VRAM(sys, &rdata, &now, addr, req, false); break;

    case 0x080 ... 0x098: // GBA Game Pak ROM
        GamePakBus_ROMRead(sys, &rdata, &now, addr, size, false); break;

    case 0x0A0 ... 0x0A8: // GBA Game Pak SRAM
        GamePakBus_RAMRead(sys, &rdata, &now, addr, size, false); break;
    }

    Bus_TransferPostSetup(sys, rdata, true, now, false, false);
}

void Bus7_Write(Console* sys, BusReq* req, timestamp now)
{
    const u32 addr = req->Addr;
    const AHB_HSIZE size = req->Size;
    const u32 width = 8<<size;
    const u32 mask = MakeWriteMask(addr, size);

    // disgusting hack
    if (req->Man7 >= MAN7_SCAPDMA0 && req->Man7 <= MAN7_NDMA3)
        req->WrData = sys->DMA7[req->Man7-MAN7_SCAPDMA0].RData;

    const u32 wrdata = req->WrData;
    // checkme: are there any devices on the bus with weird handling of addr misalignment or weird access widths?

    switch((addr>>20) & 0xFF8) // check most signficant 9 bits
    {
    case 0x020 ... 0x028: // Main RAM
        return MainRAM_Request(sys, now, false); // defer completion of req to main ram handler

    case 0x030: // Shared WRAM
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
        AddBusContention(sys, now, Dev_WRAM7 /* checkme: probably wrong? */);
        MemoryWrite(32, sys->ARM7WRAM, addr, ARM7WRAM_Size, wrdata, mask);
        break;

    case 0x040: // Memory Mapped IO
        return Sched_AddEvent(sys, now, Evt_IO7); // io is in a separate event for simplicity's sake

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
        break;
    }

    Bus_TransferPostSetup(sys, 0, false, now, false, false);
}

void Bus9_KillBursts(Console* sys, const timestamp now)
{
    if (sys->BusMR.CurReq == MainRAM_A9)
        MainRAM_KillBurst(sys, now);

    // TODO: kill gba rom/ram chipsel
}

void Bus9_Idle(Console* sys, const timestamp now)
{
    // no access performed this cycle
    // speculation: treat as signal to kill bursts
    Bus9_KillBursts(sys, now);
}

void Bus9_Busy(Console* sys [[maybe_unused]], const timestamp now [[maybe_unused]])
{
    // no access performed this cycle
    // speculation: do *not* kill bursts
}

void Bus7_KillBursts(Console* sys, const timestamp now)
{
    if (sys->BusMR.CurReq == MainRAM_A7)
        MainRAM_KillBurst(sys, now);

    // TODO: kill gba rom/ram chipsel
}

void Bus7_Idle(Console* sys, const timestamp now)
{
    // no access performed this cycle
    // speculation: treat as signal to kill bursts
    Bus7_KillBursts(sys, now);
}

void Bus7_Busy(Console* sys [[maybe_unused]], const timestamp now [[maybe_unused]])
{
    // no access performed this cycle
    // speculation: do *not* kill bursts
}

void Bus_Req(Console* sys, const BusReq* req, const timestamp now, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);

    if (bus->ReqList & (1<<(req->Man))) LogPrint(LOG_ALWAYS, "BUS REQ OVERWRITE!!!!! %"PRIu8"\n", req->Man);
    bus->ReqList |= (1<<(req->Man));
    bus->Reqs[req->Man] = *req;

    if (!bus->LockSched)
    {
        bus->LockSched = true;
        Sched_AddEventIfEarlier(sys, now, a9 ? Evt_Bus9Cmp : Evt_Bus7Cmp);
    }
}

void Bus7_A7Wake(Console* sys, const timestamp now)
{
    sys->A7ClkDisable = false;
    if (sys->Bus7.ReqList & (1<<MAN7_ARM7))
    {
        if (!sys->Bus7.LockSched)
        {
            sys->Bus7.LockSched = true;
            Sched_AddEventIfEarlier(sys, now, Evt_Bus7Cmp);
        }
    }
    else
    {
        CrashSpectacularly("what?\n");
    }
}

void Bus_TransferPostSetup(Console* sys, const u32 rdata, const bool isread, const timestamp end, const bool noprev, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);
    if (isread) bus->ReadBus = rdata;
    bus->NoPrev = noprev;
    Sched_AddEvent(sys, end, a9 ? Evt_Bus9Cmp : Evt_Bus7Cmp);
}

#define reqlista7deny (((a9 || !sys->A7ClkDisable) ? u32_max : ~(1<<MAN7_ARM7)) & bus->ReqList) // speculative method of implementing arm7 halt

void Bus_RunCmp(Console* sys, timestamp now, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);
    u32 rdata = bus->ReadBus;
    BusCallbacks cmpcb = CB_None;
    bus->CmpMan = MAN_NONE;
    bool wasload = false;

    now += DSClk33(1);
    timestamp len = DSClk33(1);

    // process last completion
    if (!bus->NoPrev)
    {
        BusReq* reqold = &bus->PipeFIFO[bus->ReqActivePtr];
        bus->CmpMan = reqold->Man;
        u8 mandebug = bus->CmpMan;
        cmpcb = reqold->CB;
        wasload = !reqold->Write;
        if (a9 && mandebug == MAN9_ARM9)
        {
            if ((cmpcb != CB9_BIU9InstrNormal) && (cmpcb != CB9_BIU9DataNormal))
                mandebug = MAN9_A9EXTERNALWONKY;
        }
        Bus_DebugBreak(sys, now, a9 ? sys->Bus9_Watch : sys->Bus7_Watch, a9 ? sys->Bus9_NumWatch : sys->Bus7_NumWatch, reqold->Addr, reqold->Size, reqold->Write, mandebug, (reqold->Write ? reqold->WrData : rdata));
        len = now - bus->PipeExitTs[bus->ReqActivePtr];
        // apply waitstate delays
        if (len > DSClk33(1))
        {
            for (size_t i = 0; i < countof(bus->PipeExitTs); i++)
                bus->PipeExitTs[i] += (len-DSClk33(1));
        }
    }

    // completion callback
    switch(cmpcb)
    {
    case CB_None: break;
    case CB9_BIU9InstrNormal ... CB9_BIU9Idle: A946_BIUCompPost(&sys->A946ES, now, rdata, cmpcb); break;
    case CB9_DMA: DMA_CompPost(sys, now, bus->CmpMan-MAN9_DMA0, rdata, wasload, true); break;
    case CB7_7TDMIData: A7TDMI_DataPost(&sys->A7TDMI, now, rdata); break;
    case CB7_7TDMIInstr: A7TDMI_InstrReadPost(&sys->A7TDMI, now, rdata); break;
    case CB7_DMA: DMA_CompPost(sys, now, bus->CmpMan-MAN7_SCAPDMA0, rdata, wasload, false); break;
    }
    Sched_AddEvent(sys, now, a9 ? Evt_Bus9Arb : Evt_Bus7Arb);
}

void Bus_RunArb(Console* sys, timestamp now, const bool a9)
{
    BusImpl* bus = (a9 ? &sys->Bus9 : &sys->Bus7);
    BusCallbacks arbcb = CB_None;
    u8 arbman;
    // arbitrate next req
    if (reqlista7deny)
    {
        u8 manager;
        // handle locked transfers
        if (bus->HLockGeneric == MAN_NONE) // not locked
            manager = stdc_trailing_zeros(reqlista7deny);
        else
        {
            manager = bus->HLockGeneric; // locked, manager stays the original

            if (!(bus->ReqList & (1<<manager))) // make sure it's actually trying to do a transfer
                goto nvm; // assume its holding lock with idle transfers
        }

        // put entry into fifo
        bus->PipeFIFO[bus->FIFOFillPtr] = bus->Reqs[manager];
        bus->PipeExitTs[bus->FIFOFillPtr] = now + bus->PipeCycles;
        arbcb = bus->Reqs[manager].CB;
        arbman = manager;

        bus->ReqList &= ~(1<<manager); // clear req list
        // update lock flag
        bus->HLockGeneric = ((bus->Reqs[manager].Lock) ? manager : MAN_NONE);

        // step fill ptr
        bus->FIFOFillPtr = (bus->FIFOFillPtr + 1) % countof(bus->PipeFIFO);
        bus->PipeNum++;
        if (bus->PipeNum > (s8)countof(bus->PipeFIFO))
        {
            Console_DebugLog(sys);
            CrashSpectacularly("PIPE OVERFILL!!!\n");
        }
        bus->FIFOEmpty = false;
    }
    nvm:

    // run callbacks
    switch(arbcb) // arbitration grant
    {
    case CB_None: break;
    case CB9_BIU9InstrNormal ... CB9_BIU9Idle: A946_BIUSubmPost(&sys->A946ES, now); break;
    case CB9_DMA: DMA_Step(sys, arbman-MAN9_DMA0, now, true); break;
    case CB7_7TDMIData: break;
    case CB7_7TDMIInstr: break;
    case CB7_DMA: DMA_Step(sys, arbman-MAN7_SCAPDMA0, now, false); break;
    }

    // try to run next access
    BusReq* req = &bus->PipeFIFO[bus->FIFODrainPtr];
    if (!bus->FIFOEmpty && ((bus->PipeExitTs[bus->FIFODrainPtr] <= now)))
    {
        bus->ReqActivePtr = bus->FIFODrainPtr;

        bus->FIFODrainPtr = (bus->FIFODrainPtr + 1) % countof(bus->PipeFIFO);
        if (bus->FIFODrainPtr == bus->FIFOFillPtr) bus->FIFOEmpty = true;
        bus->PipeNum--;
        if (bus->PipeNum < 0)
        {
            Console_DebugLog(sys);
            CrashSpectacularly("PIPE OVERFILL!!!\n");
        }

        if (req->Type >= HTRANS_NONSEQ)
        {
            if (req->Man != bus->CmpMan) req->Type = HTRANS_NONSEQ; // note: this should technically be enforced by the manager

            // split bursts on nonsequential access
            if (req->Type == HTRANS_NONSEQ) a9 ? Bus9_KillBursts(sys, now) : Bus7_KillBursts(sys, now);
            // its time; begin transfer!
            if (req->Write) a9 ? Bus9_Write(sys, req, now) : Bus7_Write(sys, req, now);
            else            a9 ? Bus9_Read(sys, req, now)  : Bus7_Read(sys, req, now);
            return; // rest of logic is for handling idle/busy cycles
        }
        else if (req->Type == HTRANS_BUSY)
        {
            if (req->Man != bus->CmpMan) a9 ? Bus9_KillBursts(sys, now) : Bus7_KillBursts(sys, now); // HACK
            if (a9) Bus9_Busy(sys, now);
            else    Bus7_Busy(sys, now);
        }
        else
        {
            // explicit idle transfer
            if (a9) Bus9_Idle(sys, now);
            else    Bus7_Idle(sys, now);
        }
        Bus_TransferPostSetup(sys, 0, false, now, false, a9);
        return;
    }

    // nothing running; implied idle transfer
    if (!bus->NoPrev) // if previous req was not an implied idle transfer, run one
    {
        if (a9) Bus9_Idle(sys, now);
        else    Bus7_Idle(sys, now);

        Bus_TransferPostSetup(sys, 0, false, now, true, a9);
        return;
    }

    // try to sleep the bus as an optimization
    timestamp new;
    if ((bus->HLockGeneric != MAN_NONE) ? (bus->ReqList & (1<<bus->HLockGeneric)) : reqlista7deny) // check if something can be granted bus
    {
        new = now;
    }
    else if (!bus->FIFOEmpty) // if something is progressing through the pipeline just wait for it's time
    {
        new = now;
        //bus->LockSched = false;
        //new = bus->PipeExitTs[bus->FIFODrainPtr]
    }
    else
    {
        bus->LockSched = false;
        return; // nothing to do; ahb go nini
    }

    Sched_AddEvent(sys, new, a9 ? Evt_Bus9Cmp : Evt_Bus7Cmp);
}
