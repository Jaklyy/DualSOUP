#include <stdckdint.h>
#include <stdbit.h>
#include <stddef.h>
#include "../../utils.h"
#include "arm.h"
#include "arm_inc.h"




union DataProc_Decode
{
    u32 Raw;
    struct
    {
        u32 : 12;
        u32 Rd : 4;
        u32 Rn : 4;
        bool SetFlags : 1;
        u32 Opcode : 4;
        bool Immediate : 1;
    };
    struct // Immediate addressing mode
    {
        u32 Imm8 : 8;
        u32 RotateImm : 4;
    };
    struct // register addressing modes
    {
        u32 Rm : 4;
        u32 ShiftType : 3;
    };
    struct // register imm shift
    {
        u32 : 7;
        u32 ShiftImm : 5;
    };
    struct // register reg shift
    {
        u32 : 8;
        u32 Rs: 4;
    };
};

u32 ARM_ADD(const u32 rn_val, const u32 shifter_out, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = ckd_add(&alu_out, (u32)rn_val, (u32)shifter_out);
    flags_out->Overflow = ckd_add(&alu_out, (s32)rn_val, (s32)shifter_out);
    return alu_out;
}

u32 ARM_ADC(const u32 rn_val, const u32 shifter_out, const bool carry_in, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    union ARM_FlagsOut flags[2];
    alu_out = ARM_ADD(rn_val, shifter_out, &flags[0]);
    alu_out |= ARM_ADD(alu_out, carry_in, &flags[1]);
    flags_out->Raw = flags[0].Raw | flags[1].Raw;
    return alu_out;
}

u32 ARM_SUB_RSB(const u32 a, const u32 b, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    flags_out->Carry    = ckd_sub(&alu_out, (u32)a, (u32)b);
    flags_out->Overflow = ckd_sub(&alu_out, (s32)a, (s32)b);
    return alu_out;
}

u32 ARM_SBC_RSC(const u32 a, const u32 b, const bool carry_in, union ARM_FlagsOut* flags_out)
{
    u32 alu_out;
    union ARM_FlagsOut flags[2];
    alu_out = ARM_SUB_RSB(a, b, &flags[0]);
    alu_out |= ARM_SUB_RSB(alu_out, carry_in, &flags[1]);
    flags_out->Raw = flags[0].Raw | flags[1].Raw;
    return alu_out;
}

void ARM_DataProc(struct ARM* cpu, const u32 instr_data)
{
    const union DataProc_Decode instr = {.Raw = instr_data};

    // barrel shifter output
    u32 shifter_out;

    union ARM_FlagsOut flags_out = {.Raw = cpu->CPSR.Flags};

    // note: register shift register variants take two cycles, and Rn is accessed on the second cycle
    // due to pipelining(?), pc is incremented after the first cycle
    // so accessing pc via Rn with these variants gets addr + 12
    u32 rn_val;
    bool twocycle = false;

    if (instr.Immediate) // Immediate
    {
        if (instr.RotateImm)
        {
            shifter_out = ROR32(instr.Imm8, instr.RotateImm*2);
            flags_out.Carry = shifter_out >> 31;
        }
        else
        {
            shifter_out = instr.Imm8;
        }
    }
    else // register
    {
        u64 rm_val = GETREG(instr.Rm);
        switch(instr.ShiftType)
        {
        case 0: // reg / lsl imm
        {
            if (instr.ShiftImm)
            {
                rm_val <<= instr.ShiftImm;
                flags_out.Carry = rm_val & ((u64)1 << 32);
            }
            break;
        }
        case 1: // lsl reg
        {
            const u8 rs_val = GETREG(instr.Rs) & 0xFF;
            twocycle = true;

            if (rs_val)
            {
                rm_val <<= rs_val;
                flags_out.Carry = rm_val & ((u64)1 << 32);
            }
            break;
        }
        case 2: // lsr imm
        {
            if (instr.ShiftImm == 0) // becomes 32
            {
                flags_out.Carry = rm_val & (1<<31);
                rm_val = 0;
            }
            else
            {
                flags_out.Carry = rm_val & (1<<(instr.ShiftImm-1));
                rm_val >>= instr.ShiftImm;
            }
            break;
        }
        case 3: // lsr reg
        {
            const u8 rs_val = GETREG(instr.Rs) & 0xFF;
            twocycle = true;

            if (rs_val)
            {
                flags_out.Carry = rm_val & (1<<(rs_val-1));
                rm_val >>= rs_val;
            }
            break;
        }
        case 4: // asr imm
        {
            u8 ShiftImm = instr.ShiftImm;
            if (ShiftImm == 0) ShiftImm = 32;

            flags_out.Carry = ((s32)rm_val >> (ShiftImm-1)) & 1;
            rm_val = (s32)rm_val >> ShiftImm;
            break;
        }
        case 5: // asr reg
        {
            const u8 rs_val = GETREG(instr.Rs) & 0xFF;
            twocycle = true;

            if (rs_val)
            {
                flags_out.Carry = ((s32)rm_val >> (rs_val-1)) & 1;
                rm_val = (s32)rm_val >> rs_val;
            }
            break;
        }
        case 6: // ror imm / rrx
        {
            if (instr.ShiftImm)
            {
                rm_val = ROR32(rm_val, instr.ShiftImm);
                flags_out.Carry = rm_val & (1<<31);
            }
            else
            {
                flags_out.Carry = rm_val & 1;
                rm_val = (cpu->CPSR.Carry << 31) | (rm_val >> 1);
            }
            break;
        }
        case 7: // ror reg
        {
            const u8 rs_val = GETREG(instr.Rs) & 0xFF;
            twocycle = true;

            if (rs_val)
            {
                rm_val = ROR32(rm_val, rs_val);
                flags_out.Carry = rm_val & (1<<31);
            }
            break;
        }
        }
        shifter_out = rm_val;
    }

    // two cycle variants increment pc before fetching rn
    if (twocycle) ARM_IncrPC(cpu, false);

    if ((instr.Opcode & 0b1101) != 0b1101) // NOT mov or mvn
        rn_val = GETREG(instr.Rn);

    if (!twocycle) ARM_IncrPC(cpu, false);

    EXECYCLES(1+twocycle, 1+twocycle, 1);

    // the actual ALU part of the ALU instruction
    u32 alu_out;
    bool overflow_out;
    switch(instr.Opcode)
    {
    case 0:  // AND
    case 8:  // TST
        alu_out = rn_val & shifter_out; break;
    case 1:  // EOR
    case 9:  // TEQ
        alu_out = rn_val ^ shifter_out; break;
    case 2:  // SUB
    case 10: // CMP
        ARM_SUB_RSB(rn_val, shifter_out, &flags_out); break;
    case 3:  // RSB
        ARM_SUB_RSB(shifter_out, rn_val, &flags_out); break;
    case 4:  // ADD
    case 11: // CMN
        ARM_ADD(rn_val, shifter_out, &flags_out); break;
    case 5:  // ADC
        ARM_ADC(rn_val, shifter_out, cpu->CPSR.Carry, &flags_out); break;
    case 6:  // SBC
        ARM_SBC_RSC(rn_val, shifter_out, cpu->CPSR.Carry, &flags_out); break;
    case 7:  // RSC
        ARM_SBC_RSC(shifter_out, rn_val, cpu->CPSR.Carry, &flags_out); break;
    case 12: // ORR
        alu_out = rn_val | shifter_out; break;
    case 13: // MOV
        alu_out = shifter_out; break;
    case 14: // BIC
        alu_out = rn_val & ~shifter_out; break;
    case 15: // MVN
        alu_out = ~shifter_out; break;
    }

    if (((instr.Opcode & 0b1100) == 0b1000)) // tst, teq, cmp, cmn
    {
        // no register writeback

        // unholy daemons awaken when the Rd field is 0xF...
        if (instr.Rd == 15)
        {
            if (cpu->CPUID == ARM7ID)
            {
                // ARM7TDMI interprets this using the standard ALU decoding logic
                // S bit is always set for these instructions and Rd is 15 which means we end up restoring the SPSR.
                // pc still isn't written back though, which results in the cpsr being updated without a pipeline flush, similarly to an MSR.
                flags_out.Negative = (alu_out >> 31);
                flags_out.Zero = !alu_out;
                cpu->CPSR.Flags = flags_out.Raw;
                // restore spsr here.
            }
            else // ARM9ID
            {
                // ARM946E-S seemingly triggers some logic from legacy -P variant instructions?
                // this actually results in r15 being written back (though no masking occurs unlike the actual legacy instructions).
                // this also results in flags NOT being set.
                SETREG(instr.Rd, alu_out, 0, 0);
            }
        }
        else
        {
            // CHECKME: it shouldn't be possible for any of these opcodes to be valid without the s bit set...?
            flags_out.Negative = (alu_out >> 31);
            flags_out.Zero = !alu_out;
            cpu->CPSR.Flags = flags_out.Raw;
        }
    }
    else
    {
        if (instr.SetFlags)
        {
            flags_out.Negative = (alu_out >> 31);
            flags_out.Zero = !alu_out;
            cpu->CPSR.Flags = flags_out.Raw;
        }

        SETREG(instr.Rd, alu_out, 0, 0);
    }
}

int ARM9_DataProc_Interlocks(struct ARM946ES* ARM9, u32 instr_data)
{
    const union DataProc_Decode instr = (union DataProc_Decode)(instr_data);

    int offset = 0;
    if (!instr.Immediate)
    {
        ARM9_CheckInterlocks(ARM9, &offset, instr.Rm, 0, false);
        if (instr.ShiftType & 0x1)
        {
            ARM9_CheckInterlocks(ARM9, &offset, instr.Rs, 0, false);
            if ((instr.Opcode & 0b1101) != 0b1101) // NOT mov or mvn
            {
                ARM9_CheckInterlocks(ARM9, &offset, instr.Rn, 1, false);
            }
        }
    }
    if ((instr.Opcode & 0b1101) != 0b1101) // NOT mov or mvn
    {
        ARM9_CheckInterlocks(ARM9, &offset, instr.Rn, 0, false);
    }
    return offset;
}

union Multiply_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 4;
        u32 Rs : 4;
        u32 Rn : 4; // also RdLo
        u32 Rd : 4; // also RdHi
        u32 Opcode : 4;
    };
    struct // opcode bits
    {
        u32 : 20;
        bool SetFlags : 1;
        bool Accumulate : 1;
        bool Unsigned : 1;
        bool Long;
    };
};

int ARM7_NumBoothIters(const u32 rs_val, const bool _unsigned)
{
    int iter = stdc_leading_zeros(rs_val);
    // signed 
    if (!_unsigned) iter |= stdc_leading_ones(rs_val);
    iter = 4 - (iter / 8);

    if (iter < 1) iter = 1;
    return iter;
}

// UMAAL should probably be in here, but the encoding is weird...?
void ARM_Mul(struct ARM* cpu, const u32 instr_data)
{
    const union Multiply_Decode instr = {.Raw = instr_data};

    u32 rm_val = GETREG(instr.Rm);
    u32 rs_val = GETREG(instr.Rs);

    ARM_IncrPC(cpu, false);

    u64 acc = 0;
    if (instr.Accumulate)
    {
        acc = GETREG(instr.Rn);
        if (instr.Long)
        {
            acc |= (u64)GETREG(instr.Rd) << 32;
        }
    }

    u64 mul_out = ((instr.Unsigned) ? ((u64)rm_val * (u64)rs_val)
                                    : ((instr.Long)
                                    ? ((s64)(s32)rm_val * (s64)(s32)rs_val)
                                    : (rm_val * rs_val)));

    mul_out += acc;

    if (cpu->CPUID == ARM7ID)
    {
        int iterations = ARM7_NumBoothIters(rs_val, instr.Unsigned);

        if (instr.SetFlags)
        {
            cpu->CPSR.Negative = (s64)mul_out < 0;
            cpu->CPSR.Zero = (instr.Long) ? !mul_out : !(u32)mul_out;
            cpu->CPSR.Carry = cpu->CPSR.Carry; // TODO: Soon...
            // NOTE: reference manual says that long multiples result in the overflow flag being unpredictable.
            // this does not apply to the ARM7TDMI to my knowledge.
        }

        // takes 1 cycle + 1 cycle per booth iteration needed to calculate.
        // one booth iteration is needed per byte of Rs.
        ARM7_ExecuteCycles((struct ARM7TDMI*)cpu, iterations + 1 + instr.Long);
    }
    else // ARM9ID
    {
        if (instr.SetFlags)
        {
            cpu->CPSR.Negative = (s64)mul_out < 0;
            cpu->CPSR.Zero = (instr.Long) ? !mul_out : !(u32)mul_out;

            ARM9_ExecuteCycles((struct ARM946ES*)cpu, 4 + instr.Long, 1);
        }
        else
        {
            // CHECKME: double check this pls.
            if (!instr.Long)
            {
                // 2 cycles effectively
                ARM9_ExecuteCycles((struct ARM946ES*)cpu, 1, 2);
            }
            else
            {
                ARM9_ExecuteCycles((struct ARM946ES*)cpu, 3, 1);
            }
        }
    }

    if (instr.Long)
    {
        if (instr.Rn != 15)
        {
            SETREG(instr.Rn, mul_out, 0, 0);
        }
        // sort of silly way to keep this compatible w/ MUL
        mul_out >>= 32;
    }

    if (instr.Rd != 15) // multiplies fail writeback to pc
    {
        SETREG(instr.Rd, mul_out, !instr.SetFlags, !instr.SetFlags);
    }
}

int ARM9_Mul_Interlocks(struct ARM946ES* ARM9, const u32 instr_data)
{
    const union Multiply_Decode instr = {.Raw = instr_data};

    int offset = 0;
    ARM9_CheckInterlocks(ARM9, &offset, instr.Rm, 0, false);
    ARM9_CheckInterlocks(ARM9, &offset, instr.Rs, 0, false);

    if (instr.Accumulate)
    {
        ARM9_CheckInterlocks(ARM9, &offset, instr.Rn, 1, true);
        if (instr.Long)
            ARM9_CheckInterlocks(ARM9, &offset, instr.Rd, 1, true);
    }
    return offset;
}

// WELCOME TO THE ARMv5+ ONLY CLUB!

union CLZ_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 8;
        u32 Rd : 4;
    };
};

void ARM_CLZ(struct ARM* cpu, const u32 instr_data)
{
    const union CLZ_Decode instr = {.Raw = instr_data};

    u32 rm_val = GETREG(instr.Rm);
    u32 alu_out = stdc_leading_zeros(rm_val);

    EXECYCLES(0, 1, 1);

    SETREG(instr.Rd, rm_val, 0, 0);
}

int ARM9_CLZ_Interlocks(struct ARM946ES* ARM9, const u32 instr_data)
{
    const union CLZ_Decode instr = {.Raw = instr_data};
    int offset = 0;

    ARM9_CheckInterlocks(ARM9, &offset, instr.Rm, 0, false);
    return offset;
}

// QADD, QDADD, QSUB, QDSUB
void ARM_Sat_Add_Sub(struct ARM* cpu, const u32 instr_data)
{

}
