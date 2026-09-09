#include "core/utils.h"
#include "../arm.h"




void A946_DumpMPU(const ARM946ES* ARM9)
{
    for (s8 i = 0; i < 8; i++)
        LogPrint(LOG_ARM9, "MPU%"PRIi8": %08"PRIX32" %08"PRIX32" %02"PRIX8" %02"PRIX8"\n", i, ARM9->CP15.MPURegionBase[i], ARM9->CP15.MPURegionMask[i], *(u8*)&ARM9->CP15.MPURegionPermsUser[i], *(u8*)&ARM9->CP15.MPURegionPermsPriv[i]);
}

void A946_ConfigureITCM(ARM946ES* ARM9)
{
    u32 size = ARM9->CP15.ITCMCR.Size + 9;
    if (size < 12) size = 12; // 4KiB min
    // CHECKME does anything interesting happen with a size >32 aka 4GiB?

    ARM9->CP15.ITCMShift = size;
}

void A946_ConfigureDTCM(ARM946ES* ARM9)
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
            ARM9->CP15.DTCMReadBase = u64_max; // sort of silly solution
        else
            ARM9->CP15.DTCMReadBase = base;
    }
    else
    {
        ARM9->CP15.DTCMReadBase = u64_max;
        ARM9->CP15.DTCMWriteBase = u64_max;
    }
}

void A946_ConfigureMPURegionSize(ARM946ES* ARM9, const u8 rgn)
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

void A946_ConfigureMPURegionPerms(ARM946ES* ARM9)
{
    for (s32 rgn = 0; rgn < 8; rgn++)
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

void A946_CP15Write(ARM946ES* ARM9, const u16 cmd, const u32 val)
{
    // try to make sure the compiler knows only 14 bits are used here.
    if (cmd >= (1<<14)) unreachable();

    switch(cmd & ((1<<14)-1))
    {
    case ARM_CoprocReg(0, 1, 0, 0): // control register
    {
        // CHECKME: bitmask
        MaskedWrite(ARM9->CP15.CR.Raw, val, 0x000FF085);
        A946_ConfigureDTCM(ARM9);
        A946_ConfigureITCM(ARM9);

        // cheaty thingy
        if (ARM9->CP15.CR.MPUEnable)
        {
            A946_ConfigureMPURegionPerms(ARM9);
            for (s32 i = 0; i < 8; i++)
                A946_ConfigureMPURegionSize(ARM9, i);
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
        A9ES_ExecuteCycles(ARM9, 1);
        break;
    }

    case ARM_CoprocReg(0, 2, 0, 0): // dcache
        ARM9->CP15.DCacheConfig = val;
        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;
    case ARM_CoprocReg(0, 2, 0, 1): // icache
        ARM9->CP15.ICacheConfig = val;
        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;
    case ARM_CoprocReg(0, 3, 0, 0): // wbuffer
        ARM9->CP15.WriteBufferConfig = val;
        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 0): // legacy data perms
        ARM9->CP15.DataPermsReg = 0;
        for (s32 i = 0; i < 8; i++)
            ARM9->CP15.DataPermsReg |= ((val & (3<<(i*2))) << (i*2));

        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 1): // legacy instr perms
        ARM9->CP15.InstrPermsReg = 0;
        for (s32 i = 0; i < 8; i++)
            ARM9->CP15.InstrPermsReg |= (val & (3<<(i*2)) << (0xF<<(i*4)));

        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 2): // data perms
        ARM9->CP15.DataPermsReg = val;
        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;

    case ARM_CoprocReg(0, 5, 0, 3): // instr perms
        ARM9->CP15.InstrPermsReg = val;
        A946_ConfigureMPURegionPerms(ARM9);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
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
        u32 rgn = ((cmd >> 3) & 0xF);
        ARM9->CP15.MPURegionCR[rgn].Raw = val & 0xFFFFF03F; // CHECKME: mask
        A946_ConfigureMPURegionSize(ARM9, rgn);
        // CHECKME: this is untested
        A9ES_ExecuteCycles(ARM9, 1);
        break;


    case ARM_CoprocReg(0, 7, 0, 4): // wait for interrupt
    case ARM_CoprocReg(0, 15, 8, 2): // wait for interrupt
        ARM9->ARM.WaitForInterrupt = true;
        break;

    case ARM_CoprocReg(0, 7, 5, 0): // flush icache
        A946_ICacheFlushAll(ARM9);
        break;
    case ARM_CoprocReg(0, 7, 5, 1): // flush icache line by addr
        A946_ICacheFlushAddr(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 6, 0): // flush dcache
        A946_DCacheFlushAll(ARM9);
        break;
    case ARM_CoprocReg(0, 7, 6, 1): // flush dcache line by addr
        A946_DCacheFlushAddr(ARM9, val);
        break;
    case ARM_CoprocReg(0, 7, 10, 1): // clean dcache line by addr
        A946_DCacheCleanAddr(ARM9, ARM9->ARM.Timestamp, val);
        break;
    case ARM_CoprocReg(0, 7, 10, 2): // clean dcache line by index + segment
        A946_DCacheCleanIdxSet(ARM9, ARM9->ARM.Timestamp, val);
        break;
    case ARM_CoprocReg(0, 7, 10, 4): // drain write buffer
        ARM9->BIU.InstrFlushWriteBuffer = true;
        A9ES_InstrBusy(ARM9);
        break;
    case ARM_CoprocReg(0, 7, 13, 1): // prefetch icache line
        A946_ICachePrefetch(ARM9, ARM9->ARM.Timestamp, val);
        break;
    case ARM_CoprocReg(0, 7, 14, 1): // clean + flush dcache line by addr
        A946_DCacheCleanFlushAddr(ARM9, ARM9->ARM.Timestamp, val);
        break;
    case ARM_CoprocReg(0, 7, 14, 2): // clean + flush dcache line by index + segment
        A946_DCacheCleanFlushIdxSet(ARM9, ARM9->ARM.Timestamp, val);
        break;

    case ARM_CoprocReg(0, 9, 1, 0): // dtcm reg
        ARM9->CP15.DTCMCR.Raw = val & 0xFFFFF03E;
        A946_ConfigureDTCM(ARM9);
        // CHECKME: this needs more testing
        A9ES_ExecuteCycles(ARM9, 1);
        break;

    case ARM_CoprocReg(0, 9, 1, 1): // itcm reg
        ARM9->CP15.ITCMCR.Raw = val & 0x3E;
        A946_ConfigureITCM(ARM9);
        // CHECKME: this needs more testing
        A9ES_ExecuteCycles(ARM9, 1);
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
        LogPrint(LOG_ARM9 | LOG_UNIMP, "ARM9 - UNIMPLEMENTED MCR CMD: %04hX %08X %08X @ %08X\n", cmd, val, ARM9->ARM.Instr[0].Raw, ARM9->ARM.PC);
        // CHECKME: this is a placeholder basically.
        A9ES_ExecuteCycles(ARM9, 1);
        break;
    }
    }
}

u32 A946_CP15Read(ARM946ES* ARM9, const u16 cmd)
{
    // try to make sure the compiler knows only 14 bits are used here.
    if (cmd >= (1<<14)) unreachable();

    // Note: I couldn't find any undocumented ways of accessing registers other than MRC2
    switch(cmd)
    {
    case ARM_CoprocReg(0, 0, 0, 0):
    case ARM_CoprocReg(0, 0, 0, 3) ... ARM_CoprocReg(0, 0, 0, 7): // idk why they duplicated this one so much but im sure there was a reason.
        return A946_IDCodeReg;

    case ARM_CoprocReg(0, 0, 0, 1):
        return A946_CacheTypeReg;

    case ARM_CoprocReg(0, 0, 0, 2):
        return A946_TCMSizeReg;


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
        for (s32 i = 0; i < 8; i++)
            ret |= (ARM9->CP15.DataPermsReg >> (i*4)) & 0b11;
        return ret;
    }

    case ARM_CoprocReg(0, 5, 0, 1):
    {
        u16 ret = 0;
        for (s32 i = 0; i < 8; i++)
            ret |= (ARM9->CP15.InstrPermsReg >> (i*4)) & 0b11;
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
