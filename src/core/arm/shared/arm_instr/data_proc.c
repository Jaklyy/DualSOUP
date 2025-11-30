#include <stdbit.h>
#include <stddef.h>
#include <stdckdint.h>
#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"




union ARM_DataProc_Decode
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

void ARM_DataProc(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_DataProc_Decode instr = {.Raw = instr_data.Raw};

    // barrel shifter output
    u32 shifter_out;

    union ARM_FlagsOut flags_out = {.Raw = cpu->CPSR.Flags};

    // note: register shift register variants take two cycles, and Rn is accessed on the second cycle
    // due to pipelining(?), pc is incremented after the first cycle
    // so accessing pc via Rn with these variants gets addr + 12
    u32 rn_val;
    bool carry_out = flags_out.Carry;

    if ((instr.Opcode & 0b1101) != 0b1101) // NOT mov or mvn
        rn_val = ARM_GetReg(instr.Rn);

    if (instr.Immediate) // Immediate
    {
        shifter_out = ARM_ROR(instr.Imm8, instr.RotateImm*2, &carry_out);
        ARM_StepPC(cpu, false);
        ARM_ExeCycles(1, 1, 1);
    }
    else // register
    {
        u32 rs_val;

        // Reg shift Reg variants are two cycles long due to needing to fetch more inputs.
        // order of operations for them is as follows:
        // Rn && Rs fetched
        // PC increments
        // Rm fetched
        if (instr.ShiftType & 0x1)
        {
            rs_val = ARM_GetReg(instr.Rs);
            ARM_StepPC(cpu, false);
            ARM_ExeCycles(2, 2, 1);
        }

        u64 rm_val = ARM_GetReg(instr.Rm);

        if (!(instr.ShiftType & 0x1))
        {
            ARM_StepPC(cpu, false);
            ARM_ExeCycles(1, 1, 1);
        }

        switch(instr.ShiftType)
        {
        case 0: // reg / lsl imm
        {
            rm_val = ARM_LSL(rm_val, instr.ShiftImm, &carry_out);
            break;
        }
        case 1: // lsl reg
        {
            rm_val = ARM_LSL(rm_val, rs_val, &carry_out);
            break;
        }
        case 2: // lsr imm
        {
            u8 shift_imm = instr.ShiftImm;
            if (shift_imm == 0) shift_imm = 32;

            rm_val = ARM_LSR(rm_val, shift_imm, &carry_out);
            break;
        }
        case 3: // lsr reg
        {
            rm_val = ARM_LSR(rm_val, rs_val, &carry_out);
            break;
        }
        case 4: // asr imm
        {
            u8 shift_imm = instr.ShiftImm;
            if (shift_imm == 0) shift_imm = 32;

            rm_val = ARM_ASR(rm_val, shift_imm, &carry_out);
            break;
        }
        case 5: // asr reg
        {
            rm_val = ARM_ASR(rm_val, rs_val, &carry_out);
            break;
        }
        case 6: // ror imm / rrx
        {
            if (instr.ShiftImm) // rotate right
            {
                rm_val = ARM_ROR(rm_val, instr.ShiftImm, &carry_out);
            }
            else // rotate right w/ extend
            {
                carry_out = rm_val & 1;
                rm_val = (cpu->CPSR.Carry << 31) | (rm_val >> 1);
            }
            break;
        }
        case 7: // ror reg
        {
            rm_val = ARM_ROR(rm_val, rs_val, &carry_out);
            break;
        }
        }
        shifter_out = rm_val;
    }
    flags_out.Carry = carry_out; 

    // the actual data processing part of the data processing instruction
    u32 alu_out;
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
        alu_out = ARM_SUB_RSB(rn_val, shifter_out, &flags_out); break;
    case 3:  // RSB
        alu_out = ARM_SUB_RSB(shifter_out, rn_val, &flags_out); break;
    case 4:  // ADD
    case 11: // CMN
        alu_out = ARM_ADD(rn_val, shifter_out, &flags_out); break;
    case 5:  // ADC
        alu_out = ARM_ADC(rn_val, shifter_out, cpu->CPSR.Carry, &flags_out); break;
    case 6:  // SBC
        alu_out = ARM_SBC_RSC(rn_val, shifter_out, cpu->CPSR.Carry, &flags_out); break;
    case 7:  // RSC
        alu_out = ARM_SBC_RSC(shifter_out, rn_val, cpu->CPSR.Carry, &flags_out); break;
    case 12: // ORR
        alu_out = rn_val | shifter_out; break;
    case 13: // MOV
        alu_out = shifter_out; break;
    case 14: // BIC
        alu_out = rn_val & ~shifter_out; break;
    case 15: // MVN
        alu_out = ~shifter_out; break;
    }

    if (instr.SetFlags)
    {
        if (instr.Rd == 15)
        {
            if ((instr.Opcode & 0b1100) == 0b1000) // tst/teq/cmp/cmn behave weirdly here
            {
                char* str;
                switch(instr.Opcode)
                {
                case 8: str = "TST"; break;
                case 9: str = "TEQ"; break;
                case 10: str = "CMP"; break;
                case 11: str = "CMN"; break;
                default: str = "Jakly, why is there an error in your error handler?"; break;
                }
                LogPrint(LOG_CPUID | LOG_ODD, "ARM%i: Executing %s with Rd == 15: %08X\n", CPUIDtoCPUNum, instr_data.Raw);
            }

            if ((cpu->CPUID == ARM9ID) && ((instr.Opcode & 0b1100) == 0b1000)) // tst/teq/cmp/cmn
            {
                // ARM946E-S seemingly triggers some logic from legacy -P variant instructions?
                // this actually results in r15 being written back (though no masking occurs unlike the actual legacy instructions).
                // flags are NOT set.
                // SPSR is NOT restored.
                ARM_SetReg(instr.Rd, alu_out, 0, 0);
            }
            else // ARM7ID && ARM9 normal instrs
            {
                // yes, this also happens for tst/teq/cmp/cmn
                // yes, that complicates things *greatly*
                ARM_RestoreSPSR;
            }
        }
        else
        {
            flags_out.Negative = (alu_out >> 31);
            flags_out.Zero = !alu_out;
            cpu->CPSR.Flags = flags_out.Raw;
        }
    }

    // tst/teq/cmp/cmn do not writeback to registers
    if ((instr.Opcode & 0b1100) != 0b1000)
    {
        ARM_SetReg(instr.Rd, alu_out, 0, 0);
    }
}

s8 ARM9_DataProc_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_DataProc_Decode instr = {.Raw = instr_data.Raw};

    s8 stall = 0;
    if (!instr.Immediate)
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
        if (instr.ShiftType & 0x1)
        {
            ARM9_CheckInterlocks(ARM9, &stall, instr.Rs, 0, false);
            if ((instr.Opcode & 0b1101) != 0b1101) // NOT mov or mvn
            {
                ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 1, false);
            }
        }
    }
    if ((instr.Opcode & 0b1101) != 0b1101) // NOT mov or mvn
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    }
    return stall;
}

union ARM_Multiply_Decode
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
        bool Signed : 1;
        bool Long : 1;
    };
};

// MUL, MLA, SMULL, SMLAL, UMULL, UMLAL
// should UMAAL be in here too...?
void ARM_Mul(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_Multiply_Decode instr = {.Raw = instr_data.Raw};

    u32 rm_val = ARM_GetReg(instr.Rm);
    u32 rs_val = ARM_GetReg(instr.Rs);

    ARM_StepPC(cpu, false);

    u64 acc;
    if (instr.Accumulate)
    {
        acc = ARM_GetReg(instr.Rn);
        if (instr.Long)
        {
            acc |= (u64)ARM_GetReg(instr.Rd) << 32;
        }
    }
    else acc = 0;

    u64 mul_out;
    if (instr.Long)
    {
        if (instr.Signed)
        {
            mul_out = (s64)(s32)rm_val * (s64)(s32)rs_val;
        }
        else
        {
            mul_out = (u64)rm_val * (u64)rs_val;
        }
        mul_out += acc;
    }
    else
    {
        // these casts are being done to make the flag setting logic work for both long and short multiplies
        mul_out = (s64)(s32)((rm_val * rs_val) + (u32)acc);
    }

    if (cpu->CPUID == ARM7ID)
    {
        int iterations = ARM7_NumBoothIters(rs_val, instr.Signed || !instr.Long);

        if (instr.SetFlags)
        {
            cpu->CPSR.Negative = (s64)mul_out < 0;
            cpu->CPSR.Zero = !mul_out;
            cpu->CPSR.Carry = cpu->CPSR.Carry; // TODO: Soon...
            // NOTE: reference manual says that long multiples result in the overflow flag being unpredictable.
            // this does not apply to the ARM7TDMI to my knowledge.
        }

        // takes 1 cycle + 1 cycle per booth iteration needed to calculate.
        // one booth iteration is needed per byte of Rs.
        ARM7_ExecuteCycles(ARM7Cast, iterations + 1 + instr.Long);
    }
    else // ARM9ID
    {
        if (instr.SetFlags)
        {
            cpu->CPSR.Negative = (s64)mul_out < 0;
            cpu->CPSR.Zero = !mul_out;

            ARM9_ExecuteCycles(ARM9Cast, 4 + instr.Long, 1);
        }
        else
        {
            // CHECKME: are these timings correct?
            if (!instr.Long)
            {
                // 2 cycles effectively
                ARM9_ExecuteCycles(ARM9Cast, 1, 2);
            }
            else
            {
                ARM9_ExecuteCycles(ARM9Cast, 3, 1);
            }
        }
    }

    // A7/9: registers are written back in the order: RdLo -> RdHi
    if (instr.Long)
    {
        if (instr.Rn != 15) // multiplies fail writeback to pc
        {
            ARM_SetReg(instr.Rn, mul_out, 0, 0);
        }
        // sort of silly way to keep this compatible w/ MUL
        mul_out >>= 32;
    }

    if (instr.Rd != 15) // multiplies fail writeback to pc
    {
        ARM_SetReg(instr.Rd, mul_out, !instr.SetFlags, !instr.SetFlags);
    }
}

s8 ARM9_Mul_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_Multiply_Decode instr = {.Raw = instr_data.Raw};

    s8 stall = 0;
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rs, 0, false);

    if (instr.Accumulate)
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 1, true);
        if (instr.Long)
            ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1 /*checkme?*/, true);
    }
    return stall;
}

// WELCOME TO THE ARMv5+ ONLY CLUB!

union ARM_CLZ_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 8;
        u32 Rd : 4;
    };
};

void ARM_CLZ(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_CLZ_Decode instr = {.Raw = instr_data.Raw};

    u32 rm_val = ARM_GetReg(instr.Rm);
    u32 alu_out = stdc_leading_zeros(rm_val);

    ARM_StepPC(cpu, false);

    ARM_ExeCycles(0, 1, 1);

    ARM_SetReg(instr.Rd, alu_out, 0, 0);
}

s8 ARM9_CLZ_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_CLZ_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    return stall;
}

union ARM_SatMath_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 8;
        u32 Rd : 4;
        u32 Rn : 4;
        u32 : 1;
        bool Sub : 1;
        bool Double : 1;
    };
};

// QADD, QDADD, QSUB, QDSUB
void ARM_SatMath(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_SatMath_Decode instr = {.Raw = instr_data.Raw};

    s64 rm_val = (s32)ARM_GetReg(instr.Rm);
    s64 rn_val = (s32)ARM_GetReg(instr.Rn);

    ARM_StepPC(cpu, false);

    s64 alu_out;
    if (instr.Double)
    {
        rn_val *= 2;

        // saturate
        if (rn_val > s32_max)
        {
            rn_val = s32_max;
            cpu->CPSR.QSticky = true;
        }
        else if (rn_val < s32_min)
        {
            rn_val = s32_min;
            cpu->CPSR.QSticky = true;
        }
    }

    if (instr.Sub)
    {
        alu_out = rm_val - rn_val;
    }
    else // add
    {
        alu_out = rm_val + rn_val;
    }

    // saturate
    if (alu_out > s32_max)
    {
        alu_out = s32_max;
        cpu->CPSR.QSticky = true;
    }
    else if (alu_out < s32_min)
    {
        alu_out = s32_min;
        cpu->CPSR.QSticky = true;
    }

    ARM_ExeCycles(0, 1, 1);

    if (instr.Rd != 15) // saturating maths dont support pc writeback
    {
        ARM_SetReg(instr.Rd, alu_out, 1, 1);
    }
}

s8 ARM9_SatMath_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_SatMath_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    return stall;
}


union ARM_HalfwordMul_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4;
        u32 : 1;
        bool X : 1;
        bool Y : 1;
        u32 : 1;
        u32 Rs : 4;
        u32 Rn : 4;
        u32 Rd : 4;
        u32 : 1;
        u32 Opcode : 2;
    };
};

void ARM_HalfwordMul(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_HalfwordMul_Decode instr = {.Raw = instr_data.Raw};

    bool oplong;
    bool opacc;
    bool opword;
    u64 acc;
    s64 mul_out;
    s32 rm_val;
    s32 rs_val;
    int memlen;
    switch(instr.Opcode)
    {
    case 0: // smla<x><y>
    {
        oplong = false;
        opword = false;
        opacc = true;
        memlen = 1;
        break;
    }
    case 1: // smlaw<y> / smulw<y>
    {
        oplong = false;
        opword = true;
        opacc = !instr.X;
        memlen = 1;
        break;
    }
    case 2: // smlal<x><y>
    {
        oplong = true;
        opword = false;
        opacc = true;
        memlen = 1;
        break;
    }
    case 3: // smul<x><y>
    {
        oplong = false;
        opword = false;
        opacc = false;
        memlen = 2;
        break;
    }
    }

    rm_val = ARM_GetReg(instr.Rm);
    if (!opword)
    {
        rm_val = (s32)(s16)(rm_val >> (16*instr.X));
    }
    rs_val = (s32)(s16)(ARM_GetReg(instr.Rs) >> (16*instr.Y));

    ARM_StepPC(cpu, false);
    ARM_ExeCycles(0, 1, memlen);

    mul_out = rm_val * rs_val;

    if (opword) mul_out >>= 16;

    if (opacc)
    {
        acc = ARM_GetReg(instr.Rn);
        if (oplong)
        {
            acc |= (u64)ARM_GetReg(instr.Rd) << 32;
        }

        if (!oplong)
        {
            s32 acc_out;
            cpu->CPSR.QSticky |= ckd_add(&acc_out, (s32)mul_out, (s32)acc);
            mul_out = acc_out;
        }
        else
        {
            mul_out += acc;
        }
    }

    // A7/9: registers are written back in the order: RdLo -> RdHi
    if (oplong)
    {
        if (instr.Rn != 15) // multiplies fail writeback to pc
        {
            // checkme: interlocks?
            ARM_SetReg(instr.Rn, mul_out, 0, 0);
        }
        // sort of silly way to keep this compatible w/ short muls
        mul_out >>= 32;
    }

    if (instr.Rd != 15) // multiplies fail writeback to pc
    {
        ARM_SetReg(instr.Rd, mul_out, 1, 1);
    }
}

s8 ARM9_HalfwordMul_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_HalfwordMul_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;
    bool oplong;
    bool opacc;
    switch(instr.Opcode)
    {
    case 0: // smla<x><y>
    {
        oplong = false;
        opacc = true;
        break;
    }
    case 1: // smlaw<y> / smulw<y>
    {
        oplong = false;
        opacc = !instr.X;
        break;
    }
    case 2: // smlal<x><y>
    {
        oplong = true;
        opacc = true;
        break;
    }
    case 3: // smul<x><y>
    {
        oplong = false;
        opacc = true;
        break;
    }
    }

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rs, 0, false);
    // CHECKME: accumulate interlock timings and port.
    if (opacc) ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 1, true);
    if (oplong) ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);

    return stall;
}