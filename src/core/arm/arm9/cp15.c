#include "../../utils.h"
#include "arm.h"
#include <__stddef_unreachable.h>




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

        u32 base = ARM9->CP15.DTCMCR.Raw >> size;

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



void ARM9_MCR_15(struct ARM946ES* ARM9, const u8 Op1, const u8 CRn, const u8 CRm, const u8 Op2, const u32 val)
{
    switch(CRn)
    {
    default:
    {
        // CHECKME: this is a placeholder basically.
        ARM9_ExecuteCycles(ARM9, 3, 1);
        return;
    }
    case 1: // control register
    {
        // CHECKME: bitmask
        ARM9->CP15.CR.Raw = val & 0x000FF087;
        // CHECKME: this is untested
        ARM9_ExecuteCycles(ARM9, 3, 1);
        return;
    }
    }
}

u32 ARM9_MRC_15(struct ARM946ES* ARM9, const u16 cmd)
{
    // try to make sure the compiler knows only 14 bits are used here.
    if (cmd >= (1<<14)) unreachable();

    // Note: I couldn't find any undocumented ways of accessing registers other than MRC2
    switch (cmd)
    {
    case ARM_CoprocReg(0, 0, 0, 0):
    case ARM_CoprocReg(0, 0, 0, 3) ... ARM_CoprocReg(0, 0, 0, 7): // idk why they duplicated this one so much but im sure there was a reason.
        return ARM9_IDCodeReg;

    case ARM_CoprocReg(0, 0, 0, 1):
        return ARM9_CacheTypeReg;

    case ARM_CoprocReg(0, 0, 0, 2):
        return ARM9_TCMSizeReg;


    case ARM_CoprocReg(0, 1, 0, 0):
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
    // Op2 = 0 or 1 both valid for backwards compatibility reasons according to docs
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


    // TODO: BIST


    // TODO: Cache Debug


    case ARM_CoprocReg(1, 15, 1, 0):
        return ARM9->CP15.TraceProcCR;

    default: // all unmapped commands return 0
        return 0;
    }
}
