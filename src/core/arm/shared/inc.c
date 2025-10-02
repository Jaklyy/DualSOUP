#include <stdckdint.h>
#include "../../utils.h"
#include "arm.h"


u32 ARM_ADD(const u32 rn_val, const u32 shifter_out, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = ckd_add((u32*)&alu_out, (u32)rn_val, (u32)shifter_out);
    flags_out->Overflow = ckd_add((s32*)&alu_out, (s32)rn_val, (s32)shifter_out);
    return alu_out;
}

u32 ARM_ADC(const u32 rn_val, const u32 shifter_out, const bool carry_in, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    union ARM_FlagsOut flags[2]; // this can be uninitialized because this instruction sets all flags
    alu_out = ARM_ADD(rn_val, shifter_out, &flags[0]);
    alu_out = ARM_ADD(alu_out, carry_in, &flags[1]);
    flags_out->Raw = flags[0].Raw | flags[1].Raw;
    return alu_out;
}

u32 ARM_SUB_RSB(const u32 a, const u32 b, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = !ckd_sub((u32*)&alu_out, (u32)a, (u32)b);
    flags_out->Overflow =  ckd_sub((s32*)&alu_out, (s32)a, (s32)b);
    return alu_out;
}

u32 ARM_SBC_RSC(const u32 a, const u32 b, const bool carry_in, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    union ARM_FlagsOut flags[2]; // this can be uninitialized because this instruction sets all flags
    alu_out = ARM_SUB_RSB(a, b, &flags[0]);
    alu_out = ARM_SUB_RSB(alu_out, !carry_in, &flags[1]);
    flags_out->Carry = flags[0].Carry & flags[1].Carry;
    flags_out->Overflow = flags[0].Overflow | flags[1].Overflow;
    return alu_out;
}


u32 ARM_LSL(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        if (shift < 64)
        {
            val <<= shift;
            *carry_out = val & ((u64)1 << 32);
        }
        else
        {
            val = 0;
            *carry_out = 0;
        }
    }
    return val;
}

u32 ARM_LSR(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        if (shift < 64)
        {
            *carry_out = val & (1<<(shift-1));
            val >>= shift;
        }
        else
        {
            val = 0;
            *carry_out = 0;
        }
    }
    return val;
}

u32 ARM_ASR(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        if (shift < 64)
        {
            // sign extend
            *carry_out = (((s64)(s32)val) >> (shift-1)) & 1;
            val = ((s64)(s32)val) >> shift;
        }
        else
        {
            val = (s32)val >> 31;
            *carry_out = val;
        }
    }
    return val;
}

u32 ARM_ROR(u64 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        val = ROR32(val, shift);
        *carry_out = val & (1<<31);
    }
    return val;
}

int ARM7_NumBoothIters(const u32 rs_val, const bool _signed)
{
    int iter = stdc_leading_zeros(rs_val);
    // signed 
    if (_signed) iter |= stdc_leading_ones(rs_val);
    iter = 4 - (iter / 8);

    if (iter < 1) iter = 1;
    return iter;
}
