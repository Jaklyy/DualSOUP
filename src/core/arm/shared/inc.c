#include <stdckdint.h>
#include "core/utils.h"
#include "arm.h"
#include "inc.h"


u32 ARM_ADD(const u32 rn_val, const u32 shifter_out, ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = ckd_add((u32*)&alu_out, (u32)rn_val, (u32)shifter_out);
    flags_out->Overflow = ckd_add((s32*)&alu_out, (s32)rn_val, (s32)shifter_out);
    return alu_out;
}

u32 ARM_ADC(const u32 rn_val, const u32 shifter_out, const bool carry_in, ARM_FlagsOut* flags_out)
{
    u64 alu_out = (u64)rn_val + (u64)shifter_out + carry_in;
    s64 salu_out = (s64)(s32)rn_val + (s64)(s32)shifter_out + carry_in;
    flags_out->Carry = (alu_out != (u32)alu_out);
    flags_out->Overflow = (salu_out != (s32)salu_out);

    return alu_out;
}

u32 ARM_SUB_RSB(const u32 a, const u32 b, ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = !ckd_sub((u32*)&alu_out, (u32)a, (u32)b);
    flags_out->Overflow =  ckd_sub((s32*)&alu_out, (s32)a, (s32)b);
    return alu_out;
}

u32 ARM_SBC_RSC(const u32 a, const u32 b, const bool carry_in, ARM_FlagsOut* flags_out)
{
    u64 alu_out = (u64)a - (u64)b - !carry_in;
    s64 salu_out = (s64)(s32)a - (s64)(s32)b - !carry_in;
    flags_out->Carry = (alu_out == (u32)alu_out);
    flags_out->Overflow = (salu_out != (s32)salu_out);

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

u32 ARM_ROR(u32 val, const u8 shift, bool* carry_out)
{
    if (shift)
    {
        val = ROR32(val, shift);
        *carry_out = val & (1<<31);
    }
    return val;
}

u8 A7TDMI_NumBoothIters(const u32 rs_val, const bool signedcheck)
{
    u8 iter = stdc_leading_zeros(rs_val);
    // signed long (and all 32 bit) multiplications use signed early termination
    if (signedcheck) iter |= stdc_leading_ones(rs_val);
    iter = 4 - (iter / 8);

    if (iter < 1) iter = 1;
    return iter;
}
