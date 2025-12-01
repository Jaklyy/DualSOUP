#include "../../utils.h"
#include "arm.h"
#include <__stddef_unreachable.h>




void ARM9_DumpMPU(const struct ARM946ES* ARM9)
{
    for (int i = 0; i < 8; i++)
        LogPrint(LOG_ARM9, "MPU%i: %08lX %08lX %02X %02X\n", i, ARM9->CP15.MPURegionBase[i], ARM9->CP15.MPURegionMask[i], ARM9->CP15.MPURegionPermsUser[i], ARM9->CP15.MPURegionPermsPriv[i]);
}

void ARM9_ConfigureITCM(struct ARM946ES* ARM9)
{
    u32 size = ARM9->CP15.ITCMCR.Size + 9;
    if (size < 12) size = 12; // 4KiB min
    // CHECKME does anything interesting happen with a size >32 aka 4GiB?

    ARM9->CP15.ITCMShift = size;
}

void ARM9_ConfigureDTCM(struct ARM946ES* ARM9)
{
    bool enabled = ARM9->CP15.CR.DTCMEnable;
    bool writeonly = ARM9->CP15.CR.DTCMLoadMode;

    if (enabled)
    {
        u32 size = ARM9->CP15.DTCMCR.Size + 9;
        if (size < 12) size = 12; // 4KiB min
        // CHECKME does anything interesting happen with a size >32 aka 4GiB?

        u32 base = (u64)ARM9->CP15.DTCMCR.Raw >> size;

        ARM9->CP15.DTCMShift = size;
        ARM9->CP15.DTCMWriteBase = base;

        if (writeonly)
        {
            // sort of silly solution
            ARM9->CP15.DTCMReadBase = u64_max;
        }
        else
        {
            ARM9->CP15.DTCMReadBase = base;
        }
    }
    else
    {
        ARM9->CP15.DTCMReadBase = u64_max;
        ARM9->CP15.DTCMWriteBase = u64_max;
    }
}

void ARM9_ConfigureMPURegionSize(struct ARM946ES* ARM9, const u8 rgn)
{
    if (!ARM9->CP15.CR.MPUEnable) return;
    if (!ARM9->CP15.MPURegionCR[rgn].Enable)
    {
        // this combination prevents it from being seen as valid without special handling.
        ARM9->CP15.MPURegionMask[rgn] = 0;
        ARM9->CP15.MPURegionBase[rgn] = u32_max;
        return;
    }

    u32 size = ARM9->CP15.MPURegionCR[rgn].Size+1;
    if (size < 12) size = 12;
    else if (size > 32) size = 32;

    // u64 to ensure shift works properly.
    ARM9->CP15.MPURegionMask[rgn] = (u32_max >> size) << size;
    ARM9->CP15.MPURegionBase[rgn] = (ARM9->CP15.MPURegionCR[rgn].Raw >> size) << size;
}

void ARM9_ConfigureMPURegionPerms(struct ARM946ES* ARM9)
{
    for (int rgn = 0; rgn < 8; rgn++)
    {
        if (!ARM9->CP15.CR.MPUEnable) continue;
        // data
        switch((ARM9->CP15.DataPermsReg >> (rgn*4)) & 0xF)
        {
        case 0:
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = false;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = false;
            break;
        case 1:
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = true;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = false;
            break;
        case 2:
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = true;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = false;
            break;
        case 3:
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = true;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = true;
            break;
        case 5:
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = true;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = false;
            break;
        case 6:
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = true;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = false;
            break;
        default: // "Unpredictable"
            LogPrint(LOG_ARM9 | LOG_UNIMP, "UNPREDICTABLE REGION PERMISSIONS %i FOR REGION %i!!\n", ((ARM9->CP15.DataPermsReg >> (rgn*4)) & 0xF), rgn);
            ARM9->CP15.MPURegionPermsPriv[rgn].Read = false;
            ARM9->CP15.MPURegionPermsPriv[rgn].Write = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Read = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Write = false;
            break;
        }

        // code
        switch((ARM9->CP15.InstrPermsReg >> (rgn*4)) & 0xF)
        {
        case 0:
            ARM9->CP15.MPURegionPermsPriv[rgn].Exec = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Exec = false;
            break;
        case 1:
        case 5:
            ARM9->CP15.MPURegionPermsPriv[rgn].Exec = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Exec = false;
            break;
        case 2:
        case 3:
        case 6:
            ARM9->CP15.MPURegionPermsPriv[rgn].Exec = true;
            ARM9->CP15.MPURegionPermsUser[rgn].Exec = true;
            break;
        default: // "Unpredictable"
            LogPrint(LOG_ARM9 | LOG_UNIMP, "UNPREDICTABLE REGION PERMISSIONS %i FOR REGION %i!!\n", ((ARM9->CP15.DataPermsReg >> (rgn*4)) & 0xF), rgn);
            ARM9->CP15.MPURegionPermsPriv[rgn].Exec = false;
            ARM9->CP15.MPURegionPermsUser[rgn].Exec = false;
            break;
        }

        ARM9->CP15.MPURegionPermsPriv[rgn].ICache = ARM9->CP15.CR.ICacheEnable && (ARM9->CP15.ICacheConfig & (1<<rgn));
        ARM9->CP15.MPURegionPermsUser[rgn].ICache = ARM9->CP15.CR.ICacheEnable && (ARM9->CP15.ICacheConfig & (1<<rgn));
        ARM9->CP15.MPURegionPermsPriv[rgn].DCache = ARM9->CP15.CR.DCacheEnable && (ARM9->CP15.DCacheConfig & (1<<rgn));
        ARM9->CP15.MPURegionPermsUser[rgn].DCache = ARM9->CP15.CR.DCacheEnable && (ARM9->CP15.DCacheConfig & (1<<rgn));
        ARM9->CP15.MPURegionPermsPriv[rgn].Buffer = ARM9->CP15.WriteBufferConfig & (1<<rgn);
        ARM9->CP15.MPURegionPermsUser[rgn].Buffer = ARM9->CP15.WriteBufferConfig & (1<<rgn);
    }
}

bool ARM9_ProgressCacheStream(timestamp* ts, struct ARM9_CacheStream* stream, u32* ret, const bool seq);

// CHECKME: does flushing cache clean the tag ram and/or cache line entirely?

void ICache_FlushAddr(struct ARM946ES* ARM9, u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, NULL, false);
    ARM9_ICacheSetLookup
    if (set < ARM9_ICacheAssoc)
    {
        ARM9->ITagRAM[index+set].Valid = false;
    }
}

void ICache_FlushAll(struct ARM946ES* ARM9)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, NULL, false);
    for (unsigned i = 0; i < (sizeof(ARM9->ITagRAM)/sizeof(ARM9->ITagRAM[0])); i++)
        ARM9->ITagRAM[i].Valid = false;
}

u32 ARM9_ICacheLookup(struct ARM946ES* ARM9, const u32 addr);

void ICache_Prefetch(struct ARM946ES* ARM9, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, NULL, false);
    ARM9_ICacheLookup(ARM9, addr);
    ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, NULL, false);
}

void DCache_FlushAddr(struct ARM946ES* ARM9, u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);
    ARM9_DCacheSetLookup
    if (set < ARM9_DCacheAssoc)
    {
        ARM9->DTagRAM[index+set].Valid = false;
    }
}

void DCache_FlushAll(struct ARM946ES* ARM9)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);
    for (unsigned i = 0; i < (sizeof(ARM9->DTagRAM)/sizeof(ARM9->DTagRAM[0])); i++)
        ARM9->DTagRAM[i].Valid = false;
}

void DCache_CleanLine(struct ARM946ES* ARM9, const u32 idxset)
{
    bool seq = false;
    u32 addr = (ARM9->DTagRAM[idxset].TagBits << 10) | (idxset >> 2 << 5);
    if (ARM9->DTagRAM[idxset].DirtyLo)
    {
        ARM9_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, addr, A9WB_Addr);
        seq = true;
        for (int i = 0; i < 4; i++)
        {
            ARM9_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, ARM9->DCache.b32[(idxset<<3)+(i*sizeof(u32))], A9WB_32);

        }
    }
    if (ARM9->DTagRAM[idxset].DirtyHi)
    {
        if (!seq)
            ARM9_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, addr+(4*sizeof(u32)), A9WB_Addr);
        for (int i = 4; i < 8; i++)
        {
            ARM9_FillWriteBuffer(ARM9, &ARM9->MemTimestamp, ARM9->DCache.b32[(idxset<<3)+(i*sizeof(u32))], A9WB_32);
        }
    }
    ARM9->DTagRAM[idxset].DirtyLo = false;
    ARM9->DTagRAM[idxset].DirtyHi = false;
}

void DCache_CleanFlushLine(struct ARM946ES* ARM9, const u32 idxset)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING

    // CHECKME: does this errata emulation all check out?
    if (ARM9->DTagRAM[idxset].DirtyLo || ARM9->DTagRAM[idxset].DirtyHi)
    {
        DCache_CleanLine(ARM9, idxset);
        ARM9->DTagRAM[idxset].Valid = false;
    }
    else if (ARM9->WBuffer.FIFOFillPtr != ARM9->WBuffer.FIFODrainPtr)
    {
        ARM9->DTagRAM[idxset].Valid = false;
    }
    else
    {
        // when the write buffer is full and the line is already clean the line will not properly be marked invalid
        LogPrint(LOG_ARM9|LOG_BUG, "ARM9 ERRATA TRIGGERED: DCACHE CLEAN+FLUSH FAILED TO FLUSH CLEAN LINE DUE TO FULL WRITE BUFFER!\n");
    }
}

void DCache_CleanIdxSet(struct ARM946ES* ARM9, const u32 val)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);

    u32 idxset = (val >> 30) | ((val >> 5) & 0x1F);

    DCache_CleanLine(ARM9, idxset);
}

void DCache_CleanFlushIdxSet(struct ARM946ES* ARM9, const u32 val)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);

    u32 idxset = (val >> 30) | ((val >> 5) & 0x1F);

    DCache_CleanLine(ARM9, idxset);
    // TODO: ERRATA
    ARM9->DTagRAM[idxset].Valid = false;
}

void DCache_CleanAddr(struct ARM946ES* ARM9, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);
    ARM9_DCacheSetLookup
    if (set < ARM9_DCacheAssoc)
    {
        DCache_CleanLine(ARM9, index | set);
    }
}

void DCache_CleanFlushAddr(struct ARM946ES* ARM9, const u32 addr)
{
    // TODO: TIMINGS
    // TODO: IMPROVE CACHE STREAMING HANDLING
    ARM9_ProgressCacheStream(&ARM9->MemTimestamp, &ARM9->DStream, NULL, false);
    ARM9_DCacheSetLookup

    if (set < ARM9_DCacheAssoc)
    {
        DCache_CleanFlushLine(ARM9, index | set);
    }
}



void ARM9_MCR_15(struct ARM946ES* ARM9, const u16 cmd, const u32 val)
{
    // try to make sure the compiler knows only 14 bits are used here.
    //if (cmd >= (1<<14)) unreachable();

    switch(cmd)
    {
    case ARM_CoprocReg(0, 1, 0, 0): // control register
    {
        // CHECKME: bitmask
        MaskedWrite(ARM9->CP15.CR.Raw, val, 0x000FF085);
        ARM9_ConfigureDTCM(ARM9);
        ARM9_ConfigureITCM(ARM9);

        // cheaty thingy
        if (ARM9->CP15.CR.MPUEnable)
        {
            ARM9_ConfigureMPURegionPerms(ARM9);
            for (int i = 0; i < 8; i++)
                ARM9_ConfigureMPURegionSize(ARM9, i);
        }
        else
        {
            // cheat to encode mpu off perms in rgn 7
            ARM9->CP15.MPURegionPermsPriv[7].Read = true;
            ARM9->CP15.MPURegionPermsPriv[7].Write = true;
            ARM9->CP15.MPURegionPermsPriv[7].Exec = true;
            ARM9->CP15.MPURegionPermsPriv[7].DCache = false;
            ARM9->CP15.MPURegionPermsPriv[7].ICache = false;
            ARM9->CP15.MPURegionPermsPriv[7].Buffer = false;
            ARM9->CP15.MPURegionPermsUser[7].Read = true;
            ARM9->CP15.MPURegionPermsUser[7].Write = true;
            ARM9->CP15.MPURegionPermsUser[7].Exec = true;
            ARM9->CP15.MPURegionPermsUser[7].DCache = false;
            ARM9->CP15.MPURegionPermsUser[7].ICache = false;
            ARM9->CP15.MPURegionPermsUser[7].Buffer = false;
            ARM9->CP15.MPURegionBase[7] = 0;
            ARM9->CP15.MPURegionMask[7] = 0;
        }
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;
    }

    case ARM_CoprocReg(0, 9, 1, 0): // dtcm reg
        ARM9->CP15.DTCMCR.Raw = val & 0xFFFFF03E;
        ARM9_ConfigureDTCM(ARM9);
        // CHECKME: this needs more testing
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    case ARM_CoprocReg(0, 9, 1, 1): // itcm reg
        ARM9->CP15.ITCMCR.Raw = val & 0x3E;
        ARM9_ConfigureITCM(ARM9);
        // CHECKME: this needs more testing
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    case ARM_CoprocReg(0, 2, 0, 0): // dcache
        ARM9->CP15.DCacheConfig = val;
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;
    case ARM_CoprocReg(0, 2, 0, 1): // icache
        ARM9->CP15.ICacheConfig = val;
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;
    case ARM_CoprocReg(0, 3, 0, 0): // wbuffer
        ARM9->CP15.WriteBufferConfig = val;
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 0): // legacy data perms
        ARM9->CP15.DataPermsReg = 0;
        for (int i = 0; i < 8; i++)
        {
            ARM9->CP15.DataPermsReg |= (val & (3<<(i*2)) << (0xF<<(i*4)));
        }
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 1): // legacy instr perms
        ARM9->CP15.InstrPermsReg = 0;
        for (int i = 0; i < 8; i++)
        {
            ARM9->CP15.InstrPermsReg |= (val & (3<<(i*2)) << (0xF<<(i*4)));
        }
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 2): // data perms
        ARM9->CP15.DataPermsReg = val;
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 3): // instr perms
        ARM9->CP15.InstrPermsReg = val;
        ARM9_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;

    // Regions: op2 == 1 is not valid for some reason?
    case ARM_CoprocReg(0, 6, 0, 0): // region 0
    case ARM_CoprocReg(0, 6, 1, 0): // region 1
    case ARM_CoprocReg(0, 6, 2, 0): // region 2 
    case ARM_CoprocReg(0, 6, 3, 0): // region 3
    case ARM_CoprocReg(0, 6, 4, 0): // region 4
    case ARM_CoprocReg(0, 6, 5, 0): // region 5
    case ARM_CoprocReg(0, 6, 6, 0): // region 6
    case ARM_CoprocReg(0, 6, 7, 0): // region 7
        unsigned rgn = ((cmd >> 3) & 0xF);
        ARM9->CP15.MPURegionCR[rgn].Raw = val & 0xFFFFF03F; // CHECKME: mask
        ARM9_ConfigureMPURegionSize(ARM9, rgn);
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;


    case ARM_CoprocReg(0, 7, 0, 4): // wait for interrupt
    case ARM_CoprocReg(0, 15, 8, 2): // wait for interrupt
        ARM9->ARM.WaitForInterrupt = true;
        break;

    case ARM_CoprocReg(0, 7, 5, 0): // flush icache
        ICache_FlushAll(ARM9);
        break;
    case ARM_CoprocReg(0, 7, 5, 1): // flush icache line by addr
        ICache_FlushAddr(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 6, 0): // flush dcache
        DCache_FlushAll(ARM9);
        break;
    case ARM_CoprocReg(0, 7, 6, 1): // flush dcache line by addr
        DCache_FlushAddr(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 10, 1): // clean dcache line by addr
        DCache_CleanAddr(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 10, 2): // clean dcache line by index + segment
        DCache_CleanFlushIdxSet(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 10, 4): // drain write buffer
        ARM9_DrainWriteBuffer(ARM9, &ARM9->MemTimestamp);
        break;
    case ARM_CoprocReg(0, 7, 13, 1): // prefetch icache line
        ICache_Prefetch(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 14, 1): // clean + flush dcache line by addr
        DCache_CleanFlushAddr(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 14, 2): // clean + flush dcache line by index + segment
        DCache_CleanFlushIdxSet(ARM9, val);
        break;

    case ARM_CoprocReg(0, 13, 0, 1): // pid
    case ARM_CoprocReg(0, 13, 1, 1): // pid
        ARM9->CP15.TraceProcIdReg = val;
        break;

    // BIST
    case ARM_CoprocReg(0, 15, 0, 0): // test state
    // TAG BIST
    case ARM_CoprocReg(0, 15, 0, 1): // tag bist cr
    case ARM_CoprocReg(0, 15, 0, 2): // itag addr
    case ARM_CoprocReg(0, 15, 0, 3): // itag gen
    case ARM_CoprocReg(0, 15, 0, 6): // dtag addr
    case ARM_CoprocReg(0, 15, 0, 7): // dtag gen
    // TCM BIST
    case ARM_CoprocReg(1, 15, 0, 1): // tcm bist cr
    case ARM_CoprocReg(1, 15, 0, 2): // itcm addr
    case ARM_CoprocReg(1, 15, 0, 3): // itcm gen
    case ARM_CoprocReg(1, 15, 0, 6): // dtcm addr
    case ARM_CoprocReg(1, 15, 0, 7): // dtcm gen
    // Cache BIST
    case ARM_CoprocReg(2, 15, 0, 1): // cache bist cr
    case ARM_CoprocReg(2, 15, 0, 2): // icache addr
    case ARM_CoprocReg(2, 15, 0, 3): // icache gen
    case ARM_CoprocReg(2, 15, 0, 6): // dcache addr
    case ARM_CoprocReg(2, 15, 0, 7): // dcache gen

    // cache debug
    case ARM_CoprocReg(3, 15, 0, 0): // index reg
    case ARM_CoprocReg(3, 15, 1, 0): // itag
    case ARM_CoprocReg(3, 15, 2, 0): // dtag
    case ARM_CoprocReg(3, 15, 3, 0): // icache
    case ARM_CoprocReg(3, 15, 4, 0): // dcache

    // trace control
    case ARM_CoprocReg(1, 15, 1, 0):
    default:
    {
        LogPrint(LOG_ARM9 | LOG_UNIMP, "ARM9 - UNIMPLEMENTED MCR CMD: %04lX %08lX %08lX @ %08lX\n", cmd, val, ARM9->ARM.Instr[0].Raw, ARM9->ARM.PC);
        // CHECKME: this is a placeholder basically.
        ARM9_ExecuteCycles(ARM9, 2, 1);
        break;
    }
    }
}

u32 ARM9_MRC_15(struct ARM946ES* ARM9, const u16 cmd)
{
    // try to make sure the compiler knows only 14 bits are used here.
    //if (cmd >= (1<<14)) unreachable();

    // Note: I couldn't find any undocumented ways of accessing registers other than MRC2
    switch(cmd)
    {
    case ARM_CoprocReg(0, 0, 0, 0):
    case ARM_CoprocReg(0, 0, 0, 3) ... ARM_CoprocReg(0, 0, 0, 7): // idk why they duplicated this one so much but im sure there was a reason.
        return ARM9_IDCodeReg;

    case ARM_CoprocReg(0, 0, 0, 1):
        return ARM9_CacheTypeReg;

    case ARM_CoprocReg(0, 0, 0, 2):
        return ARM9_TCMSizeReg;


    case ARM_CoprocReg(0, 1, 0, 0): // control reg
        return ARM9->CP15.CR.Raw;


    case ARM_CoprocReg(0, 2, 0, 0):
        return ARM9->CP15.DCacheConfig;

    case ARM_CoprocReg(0, 2, 0, 1):
        return ARM9->CP15.ICacheConfig;

    case ARM_CoprocReg(0, 3, 0, 0):
        return ARM9->CP15.WriteBufferConfig;


    // legacy permissions registers; only displays low 2 bits of each value.
    case ARM_CoprocReg(0, 5, 0, 0):
    {
        u16 ret = 0;
        for (int i = 0; i < 8; i++)
        {
            ret |= (ARM9->CP15.DataPermsReg >> (i*4)) & 0b11;
        }
        return ret;
    }

    case ARM_CoprocReg(0, 5, 0, 1):
    {
        u16 ret = 0;
        for (int i = 0; i < 8; i++)
        {
            ret |= (ARM9->CP15.InstrPermsReg >> (i*4)) & 0b11;
        }
        return ret;
    }

    // extended permissions registers; full register contents shown.
    case ARM_CoprocReg(0, 5, 0, 2):
        return ARM9->CP15.DataPermsReg;

    case ARM_CoprocReg(0, 5, 0, 3):
        return ARM9->CP15.InstrPermsReg;


    // region perms
    // Op2 == 0 or 1 both valid for backwards compatibility reasons according to docs
    case ARM_CoprocReg(0, 6, 0, 0): case ARM_CoprocReg(0, 6, 0, 1):
    case ARM_CoprocReg(0, 6, 1, 0): case ARM_CoprocReg(0, 6, 1, 1):
    case ARM_CoprocReg(0, 6, 2, 0): case ARM_CoprocReg(0, 6, 2, 1):
    case ARM_CoprocReg(0, 6, 3, 0): case ARM_CoprocReg(0, 6, 3, 1):
    case ARM_CoprocReg(0, 6, 4, 0): case ARM_CoprocReg(0, 6, 4, 1):
    case ARM_CoprocReg(0, 6, 5, 0): case ARM_CoprocReg(0, 6, 5, 1):
    case ARM_CoprocReg(0, 6, 6, 0): case ARM_CoprocReg(0, 6, 6, 1):
    case ARM_CoprocReg(0, 6, 7, 0): case ARM_CoprocReg(0, 6, 7, 1):
        return ARM9->CP15.MPURegionCR[(cmd >> 3) & 0x7].Raw;


    case ARM_CoprocReg(0, 9, 0, 0):
        return ARM9->CP15.DCacheLockdownCR.Raw;

    case ARM_CoprocReg(0, 9, 0, 1):
        return ARM9->CP15.ICacheLockdownCR.Raw;


    case ARM_CoprocReg(0, 9, 1, 0):
        return ARM9->CP15.DTCMCR.Raw;

    case ARM_CoprocReg(0, 9, 1, 1):
        return ARM9->CP15.ITCMCR.Raw;


    case ARM_CoprocReg(0, 13, 0, 1):
    case ARM_CoprocReg(0, 13, 1, 1): // arm supports this for compat reasons idk
        return ARM9->CP15.TraceProcIdReg;

    // BIST
    case ARM_CoprocReg(0, 15, 0, 0): // test state
    // TAG BIST
    case ARM_CoprocReg(0, 15, 0, 1): // tag bist cr
    case ARM_CoprocReg(0, 15, 0, 2): // itag addr
    case ARM_CoprocReg(0, 15, 0, 3): // itag gen
    case ARM_CoprocReg(0, 15, 0, 6): // dtag addr
    case ARM_CoprocReg(0, 15, 0, 7): // dtag gen
    // TCM BIST
    case ARM_CoprocReg(1, 15, 0, 1): // tcm bist cr
    case ARM_CoprocReg(1, 15, 0, 2): // itcm addr
    case ARM_CoprocReg(1, 15, 0, 3): // itcm gen
    case ARM_CoprocReg(1, 15, 0, 6): // dtcm addr
    case ARM_CoprocReg(1, 15, 0, 7): // dtcm gen
    // Cache BIST
    case ARM_CoprocReg(2, 15, 0, 1): // cache bist cr
    case ARM_CoprocReg(2, 15, 0, 2): // icache addr
    case ARM_CoprocReg(2, 15, 0, 3): // icache gen
    case ARM_CoprocReg(2, 15, 0, 6): // dcache addr
    case ARM_CoprocReg(2, 15, 0, 7): // dcache gen

    // cache debug
    case ARM_CoprocReg(3, 15, 0, 0): // index reg
    case ARM_CoprocReg(3, 15, 1, 0): // itag
    case ARM_CoprocReg(3, 15, 2, 0): // dtag
    case ARM_CoprocReg(3, 15, 3, 0): // icache
    case ARM_CoprocReg(3, 15, 4, 0): // dcache
        LogPrint(LOG_ARM9 | LOG_UNIMP, "ARM9 - UNIMPLEMENTED MRC CMD: %i\n", cmd);
        return 0;

    case ARM_CoprocReg(1, 15, 1, 0):
        return ARM9->CP15.TraceProcCR;

    default: // all unmapped commands return 0
        LogPrint(LOG_ARM9 | LOG_ODD, "ARM9 - INVALID MRC CMD: %i\n", cmd);
        return 0;
    }
}
