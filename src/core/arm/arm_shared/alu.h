#include "../../utils.h"
#include "arm.h"




#include "definewall.h"

union ALU_Decode
{
    u32 data;
    struct
    {
        u32 : 12;
        u32 rd : 4;
        u32 rn : 4;
        bool setflags : 1;
        u32 opcode : 4;
        bool immediate : 1;
    };
    struct // immediate addressing mode
    {
        u32 imm8 : 8;
        u32 rotate_imm : 4;
    };
    struct // register addressing modes
    {
        u32 rm : 4;
        u32 shift_type : 3;
    };
    struct // register imm shift
    {
        u32 : 7;
        u32 shift_imm : 5;
    };
    struct // register reg shift
    {
        u32 : 8;
        u32 rs : 4;
    };
};

void NAME(ARM, ALU)(struct ARM* cpu, u32 instr_data)
{
    const union ALU_Decode instr = (union ALU_Decode)(instr_data);

    // barrel shifter output
    u32 shifter_out;

    // this is only used when the S bit is set in the instruction
    // and if the alu op doesn't set the carry flag on its own.
    bool carry_out;

    // note: register shift register variants take two cycles, and rn is accessed on the second cycle
    // due to pipelining, pc is incremented after the first cycle
    // so accessing pc via rn with these variants gets addr + 12
    u32 rn_val;

    if (instr.immediate) // immediate
    {
        if (instr.rotate_imm)
        {
            shifter_out = ROR32(instr.imm8, instr.rotate_imm*2);
            carry_out = shifter_out >> 31;
        }
        else
        {
            shifter_out = instr.imm8;
        }
    }
    else // register
    {
        u64 rm_val = GETREG(CAST, instr.rm, 0, false);
        switch(instr.shift_type)
        {
        case 0: // reg / lsl imm
        {
            if (instr.shift_imm)
            {
                rm_val <<= instr.shift_imm;
                carry_out = rm_val & ((u64)1 << 32);
            }
            else carry_out = cpu->CPSR.Carry;
            break;
        }
        case 1: // lsl reg
        {
            const u8 rs_val = GETREG(CAST, instr.rs) & 0xFF;
            ARM_IncrPC(cpu, false);

            if (rs_val)
            {
                rm_val <<= rs_val;
                carry_out = rm_val & ((u64)1 << 32);
            }
            else carry_out = cpu->CPSR.Carry;
            break;
        }
        case 2: // lsr imm
        {
            if (instr.shift_imm == 0) // becomes 32
            {
                carry_out = rm_val & (1<<31);
                rm_val = 0;
            }
            else
            {
                carry_out = rm_val & (1<<(instr.shift_imm-1));
                rm_val >>= instr.shift_imm;
            }
            break;
        }
        case 3: // lsr reg
        {
            const u8 rs_val = GETREG(CAST, instr.rs) & 0xFF;
            ARM_IncrPC(cpu, false);

            if (rs_val)
            {
                carry_out = rm_val & (1<<(rs_val-1));
                rm_val >>= rs_val;
            }
            else carry_out = cpu->CPSR.Carry;
            break;
        }
        case 4: // asr imm
        {
            u8 shift_imm = instr.shift_imm;
            if (shift_imm == 0) shift_imm = 32;

            carry_out = ((s32)rm_val >> (shift_imm-1)) & 1;
            rm_val = (s32)rm_val >> shift_imm;
            break;
        }
        case 5: // asr reg
        {
            const u8 rs_val = GETREG(CAST, instr.rs) & 0xFF;
            ARM_IncrPC(cpu, false);

            if (rs_val)
            {
                carry_out = ((s32)rm_val >> (rs_val-1)) & 1;
                rm_val = (s32)rm_val >> rs_val;
            }
            else carry_out = cpu->CPSR.Carry;
            break;
        }
        case 6: // ror imm / rrx
        {
            if (instr.shift_imm)
            {
                rm_val = ROR32(rm_val, instr.shift_imm);
                carry_out = rm_val & (1<<31);
            }
            else
            {
                carry_out = rm_val & 1;
                rm_val = (cpu->CPSR.Carry << 31) | (rm_val >> 1);
            }
            break;
        }
        case 7: // ror reg
        {
            const u8 rs_val = GETREG(CAST, instr.rs) & 0xFF;
            ARM_IncrPC(cpu, false);

            if (rs_val)
            {
                rm_val = ROR32(rm_val, rs_val);
                carry_out = rm_val & (1<<31);
            }
            else carry_out = cpu->CPSR.Carry;
            break;
        }
        }
        shifter_out = rm_val;
    }

    // checkme: does this apply to mov/mvn? they still only need to fetch 2 operands.
    if ((instr.opcode != 13) && (instr.opcode != 15))
        rn_val = GETREG(CAST, instr.rn);

    // oh right there's an actual instruction we need to execute still
    switch(instr.opcode)
    {
    case 0:  NAME(ARM, AND)    (cpu, instr, rn_val, shifter_out, carry_out); break;
    case 1:  NAME(ARM, EOR)    (cpu, instr, rn_val, shifter_out, carry_out); break;
    case 2:  NAME(ARM, SUB_RSB)(cpu, instr, rn_val, shifter_out           ); break;
    case 3:  NAME(ARM, SUB_RSB)(cpu, instr, shifter_out, rn_val           ); break;
    case 4:  NAME(ARM, ADD)    (cpu, instr, rn_val, shifter_out           ); break;
    case 5:  NAME(ARM, ADC)    (cpu, instr, rn_val, shifter_out           ); break;
    case 6:  NAME(ARM, SBC_RSC)(cpu, instr, rn_val, shifter_out           ); break;
    case 7:  NAME(ARM, SBC_RSC)(cpu, instr, shifter_out, rn_val           ); break;
    case 8:  NAME(ARM, TST)    (cpu, instr, rn_val, shifter_out, carry_out); break;
    case 9:  NAME(ARM, TEQ)    (cpu, instr, rn_val, shifter_out, carry_out); break;
    case 10: NAME(ARM, CMP)    (cpu, instr, rn_val, shifter_out           ); break;
    case 11: NAME(ARM, CMN)    (cpu, instr, rn_val, shifter_out           ); break;
    case 12: NAME(ARM, ORR)    (cpu, instr, rn_val, shifter_out, carry_out); break;
    case 13: NAME(ARM, MOV)    (cpu, instr,         shifter_out, carry_out); break;
    case 14: NAME(ARM, BIC)    (cpu, instr, rn_val, shifter_out, carry_out); break;
    case 15: NAME(ARM, MVN)    (cpu, instr,         shifter_out, carry_out); break;
    }

    // increment pc now if it wasn't already incremented, otherwise add one more cycle
    if (instr.immediate || !(instr.shift_type & 1))
    {
        ARM_IncrPC(cpu, false);
    }
    else
    {
        ADDCYCLES(cpu, 1);
    }
}

#include "definewall.h"
