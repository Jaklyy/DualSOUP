#include "arm.h"
#include "../../bus/ahb.h"




u32 ARM7_BusRead(struct ARM7TDMI* ARM7, const u32 addr, const u32 mask, bool* seq)
{
    if (!AHB_NegOwnership(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, false, false))
        *seq = false;
    u32 val = AHB7_Read(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, addr, mask, false, false, seq, true, ARM7->ARM.PC);
    *seq = true;
    return val & mask;
}

void ARM7_BusWrite(struct ARM7TDMI* ARM7, const u32 addr, const u32 val, const u32 mask, const u32 atomic, bool* seq)
{
    if (!AHB_NegOwnership(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, atomic, false))
        *seq = false;
    AHB7_Write(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, addr, val, mask, atomic, seq, true, ARM7->ARM.PC);
    *seq = true;
}

void ARM7_InstrRead32(struct ARM7TDMI* ARM7, const u32 addr)
{
    u32 instr = ARM7_BusRead(ARM7, addr, u32_max, &ARM7->ARM.CodeSeq);
    ARM7->ARM.Instr[2] = (struct ARM_Instr){.Raw = instr, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                            .Aborted = false, // not used
                                            .CoprocPriv = false, // not used
                                            .Flushed = false};
}

void ARM7_InstrRead16(struct ARM7TDMI* ARM7, const u32 addr)
{
    u32 instr = ARM7_BusRead(ARM7, addr, u16_max << ((addr & 2)*8), &ARM7->ARM.CodeSeq);
    instr = (instr >> ((addr & 2)*8)) & 0xFFFF;
    ARM7->ARM.Instr[2] = (struct ARM_Instr){.Raw = instr, // encode bkpt as a minor hack to avoid needing dedicated prefetch abort handling.
                                            .Aborted = false, // not used
                                            .CoprocPriv = false, // not used
                                            .Flushed = false};
}
