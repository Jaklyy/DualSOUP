#include "arm.h"
#include "core/bus/bus.h"
#include "core/arm/shared/arm.h"
#include "core/scheduler.h"




void A7TDMI_BusRead(ARM7TDMI* a7tdmi, const timestamp now, u32 addr, const ARM_DataWidth size, const bool nseq)
{
    // nds hardware seems to force align words (but not halfwords!?) when accessing the gba sram interface.
    // this still occurs in gba mode (on 3ds at least) and the same code run on actual gba hardware will result in unaligned accesses as expected.
    // so im suspecting, but unable to outright confirm, a cpu revision difference.
    // though it could be a difference anywhere from the arm7tdmi bus interface, the bus itself, the gba cart sram interface, etc.
    // but it is almost certainly a hardware revision somewhere along the chain.
    if (size == ARMDataWidth_32) addr &= ~3;

    BusReq req = {
        .Addr = addr,
        .WrVal = 0,
        .Write = false,
        .Lock = false,
        .Man7 = MAN7_ARM7,
        .Prot = {.Data = true}, // other signals might just not exist on arm7 bus?
        .Size = size,
        .Type = (nseq ? HTRANS_NONSEQ : HTRANS_SEQ),
        .CB = 0,
    };
    Bus_Req(a7tdmi->ARM.Sys, &req, now, false);
}

void ARM7_BusWrite(ARM7TDMI* ARM7, const u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq)
{
    // TODO: how is misalignment of address handled?
    if (!AHB_NegOwnership(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, atomic, false))
        *seq = false;
    AHB7_Write(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, addr, val, mask, atomic, seq, true, ARM7->ARM.PC);
    *seq = true;
}

void A7TDMI_RotateExtendUnit(u32* rdata, const u32 addr, const ARM_DataWidth size, const bool signext)
{
    switch (size)
    {
    case ARMDataWidth_8:
    {
        *rdata = ROR32(*rdata, ((addr & 0x3) * 8));

        if (signext) *rdata = (s32)(s8)*rdata;
        else *rdata &= 0xFF; // zero extend
        break;
    }
    case ARMDataWidth_16:
    {
        // misaligned halfword reads are weird on ARM7TDMI
        // it selects unused byte lanes to zero/sign fill using bit 1 of the address (properly halfword aligned)
        // but then does the ASR/ROR to put them into place using bits 1 & 0 (not aligned!!!)
        // this results in misaligned ldrsh giving behavior similar to ldrsb and misaligned ldrh putting a byte into the high portion of the register
        // presumably the halfword select logic is unique, but the ASR/ROR logic is reused from byte/word fetch logic
        if (signext)
        {
            // put sign bit in high lanes and arithmetic right shift them into the proper spot
            if (!(addr & 0x2)) *rdata = (s32)(s16)*rdata;
            *rdata = ((s32)*rdata) >> ((addr&0x3)*8);
        }
        else
        {
            // isolate byte lanes and then rotate them into place
            *rdata &= (0xFFFF << ((addr&0x2)*8));
            *rdata = ROR32(*rdata, ((addr&0x3)*8));
        }
        break;
    }
    case ARMDataWidth_32:  *rdata = ROR32(*rdata, ((addr & 0x3) * 8)); break;
    default: unreachable();
    }
}

void ARM7_InstrRead32(ARM7TDMI* ARM7, const u32 addr)
{
    // nds seems to force align words even for gba sram
    // this case is known to be unaligned on gba so im suspecting, but unable to outright confirm, a cpu revision difference?
    u32 instr = ARM7_BusRead(ARM7, addr & ~3, HSIZE_32, &ARM7->ARM.CodeSeq);
    ARM7->ARM.Instr[2] = (ARM_Instr){.Raw = instr,
                                            .Aborted = false, // only used in theory
                                            .CoprocPriv = false}; // this is for an arm9 specific bug
}

void ARM7_InstrRead16(ARM7TDMI* ARM7, const u32 addr)
{
    u32 instr = ARM7_BusRead(ARM7, addr, HSIZE_16, &ARM7->ARM.CodeSeq);
    instr = (instr >> ((addr & 2)*8)) & 0xFFFF;
    ARM7->ARM.Instr[2] = (ARM_Instr){.Raw = instr,
                                            .Aborted = false, // only used in theory
                                            .CoprocPriv = false}; // this is for an arm9 specific bug
}
