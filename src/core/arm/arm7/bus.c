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

u32 ARM7_DataRead32(struct ARM7TDMI* ARM7, const u32 addr, bool* seq)
{
    return ARM7_BusRead(ARM7, addr, u32_max, seq);
}

u32 ARM7_DataRead16(struct ARM7TDMI* ARM7, const u32 addr, bool* seq)
{
    u32 mask = ROL32(u16_max, ((addr & 2) * 8));
    return ARM7_BusRead(ARM7, addr, mask, seq);
}

u32 ARM7_DataRead8(struct ARM7TDMI* ARM7, const u32 addr, bool* seq)
{
    u32 mask = ROL32(u8_max, ((addr & 3) * 8));
    return ARM7_BusRead(ARM7, addr, mask, seq);
}

void ARM7_BusWrite(struct ARM7TDMI* ARM7, const u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq)
{
    if (!AHB_NegOwnership(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, atomic, false))
        *seq = false;
    //if ((addr & 0xFF000000) == 0x02000000) printf("AA %08X\n", addr);
    AHB7_Write(ARM7->ARM.Sys, &ARM7->ARM.Timestamp, addr, val, mask, atomic, seq, true, ARM7->ARM.PC);
    *seq = true;
}

void ARM7_DataWrite32(struct ARM7TDMI* ARM7, const u32 addr, u32 val, const bool atomic, bool* seq)
{
    ARM7_BusWrite(ARM7, addr, val, u32_max, atomic, seq);
}

void ARM7_DataWrite16(struct ARM7TDMI* ARM7, const u32 addr, u32 val, bool* seq)
{
    val = ROL32(val, ((addr & 2) * 8));
    u32 mask = ROL32(u16_max, ((addr & 2) * 8));
    ARM7_BusWrite(ARM7, addr, val, mask, false, seq);
}

void ARM7_DataWrite8(struct ARM7TDMI* ARM7, const u32 addr, u32 val, const bool atomic, bool* seq)
{
    val = ROL32(val, ((addr & 3) * 8));
    u32 mask = ROL32(u8_max, ((addr & 3) * 8));
    ARM7_BusWrite(ARM7, addr, val, mask, atomic, seq);
}

void ARM7_InstrRead32(struct ARM7TDMI* ARM7, const u32 addr)
{
    u32 instr = ARM7_BusRead(ARM7, addr, u32_max, &ARM7->ARM.CodeSeq);
    ARM7->ARM.Instr[2] = (struct ARM_Instr){.Raw = instr,
                                            .Aborted = false, // not used
                                            .CoprocPriv = false, // not used
                                            .Flushed = false};
}

void ARM7_InstrRead16(struct ARM7TDMI* ARM7, const u32 addr)
{
    u32 instr = ARM7_BusRead(ARM7, addr, u16_max << ((addr & 2)*8), &ARM7->ARM.CodeSeq);
    instr = (instr >> ((addr & 2)*8)) & 0xFFFF;
    ARM7->ARM.Instr[2] = (struct ARM_Instr){.Raw = instr,
                                            .Aborted = false, // not used
                                            .CoprocPriv = false, // not used
                                            .Flushed = false};
}
