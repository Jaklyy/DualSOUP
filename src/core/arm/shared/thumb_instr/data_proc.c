#include "core/utils.h"
#include "../arm.h"
#include "../inc.h"




union THUMB_ShiftImm_Decode
{
    u16 Raw;
    struct
    {
        u16 Rd : 3;
        u16 Rm : 3;
        u16 ShiftImm : 5;
        u16 Opcode : 2;
    };
};

void THUMB_ShiftImm(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_ShiftImm_Decode instr = {.Raw = instr_data.Raw};

    u64 rm_val = ARM_GetReg(instr.Rm);

    bool carry_out = cpu->CPSR.Carry;

    switch(instr.Opcode)
    {
    case 0: // LSL
    {
        rm_val = ARM_LSL(rm_val, instr.ShiftImm, &carry_out);
        break;
    }
    case 1: // LSR
    {
        u8 shift_imm = instr.ShiftImm;
        if (shift_imm == 0) shift_imm = 32;

        rm_val = ARM_LSR(rm_val, shift_imm, &carry_out);
        break;
    }
    case 2: // ASR
    {
        u8 shift_imm = instr.ShiftImm;
        if (shift_imm == 0) shift_imm = 32;

        rm_val = ARM_ASR(rm_val, shift_imm, &carry_out);
        break;
    }
    default:
        unreachable();
    }

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    cpu->CPSR.Negative = rm_val >> 31;
    cpu->CPSR.Zero = !rm_val;
    cpu->CPSR.Carry = carry_out;

    ARM_SetReg(instr.Rd, rm_val);
}

s8 T9ES_ShiftImm_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_ShiftImm_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rm == reg) return len;

    if ((len > 1) && (instr.Rd != reg)) *retry = true;
    return 0;
}


union THUMB_AddSub_Decode
{
    u16 Raw;
    struct
    {
        u16 Rd : 3;
        u16 Rn : 3;
        u16 Rm : 3;
        bool Subtract : 1;
        bool Immediate : 1;
    };
    struct
    {
        u16 : 6;
        u16 Imm3 : 3;
    };
};

void THUMB_AddSub(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_AddSub_Decode instr = {.Raw = instr_data.Raw};

    ARM_FlagsOut flags_out;
    const u32 rn_val = ARM_GetReg(instr.Rn);
    // handle both imm3 and register variants here
    const u32 rm_val = (instr.Immediate) ? instr.Imm3 : ARM_GetReg(instr.Rm);

    u32 alu_out;
    if (instr.Subtract) alu_out = ARM_SUB_RSB(rn_val, rm_val, &flags_out);
    else                alu_out = ARM_ADD    (rn_val, rm_val, &flags_out);

    flags_out.Negative = alu_out >> 31;
    flags_out.Zero = !alu_out;

    // these instructions set flags
    cpu->CPSR.Flags = flags_out.Raw;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    ARM_SetReg(instr.Rd, alu_out);
}

s8 THUMB9_AddSub_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_AddSub_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rn == reg) return len;
    if (!instr.Immediate && (instr.Rm == reg)) return len;

    if ((len > 1) && (instr.Rd != reg)) *retry = true;
    return 0;
}

union THUMB_DataProcImm8_Decode
{
    u16 Raw;
    struct
    {
        u16 Imm8 : 8;
        u16 Rd : 3;
        u16 Opcode : 2;
    };
};

void THUMB_MovsImm8(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};

    // this instruction sets flags for some unfathomable reason.
    cpu->CPSR.Negative = 0; // the immediate is not signed. this cannot be set.
    cpu->CPSR.Zero = !instr.Imm8;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    ARM_SetReg(instr.Rd, instr.Imm8);
}

s8 THUMB9_MovsImm8_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len [[maybe_unused]], const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};

    if ((len > 1) && (instr.Rd != reg)) *retry = true;
    return 0;
}

void THUMB_DataProcImm8(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};

    // mov is handled in a separate function since it really has no reason to share any logic.
    if (instr.Opcode == 0) unreachable();

    ARM_FlagsOut flags_out;
    u32 rd_val = ARM_GetReg(instr.Rd);

    if (instr.Opcode == 2) // ADDS
        rd_val = ARM_ADD(rd_val, instr.Imm8, &flags_out);
    else // SUBS/CMP
        rd_val = ARM_SUB_RSB(rd_val, instr.Imm8, &flags_out);

    flags_out.Negative = rd_val >> 31;
    flags_out.Zero = !rd_val;

    cpu->CPSR.Flags = flags_out.Raw;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    if (instr.Opcode != 1) // not CMP
        ARM_SetReg(instr.Rd, rd_val);
}

s8 THUMB9_DataProcImm8_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rd == reg) return len;

    if (len > 1) *retry = true; // if we reach this point retry is always true
    return 0;
}

union THUMB_DataProcReg_Decode
{
    u16 Raw;
    struct
    {
        u16 Rd : 3;
        u16 Rm : 3;
        u16 Opcode : 4;
    };
    struct
    {
        u16 Rn : 3;
        u16 Rs : 3;
    };
};

void THUMB_DataProcReg(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_DataProcReg_Decode instr = {.Raw = instr_data.Raw};

    // RSBS (NEGS) uses a fixed value of 0.
    // MVNS doesn't fetch any value for rd
    const u32 rd_val = (((instr.Opcode == 9) || (instr.Opcode == 15)) ? 0 : ARM_GetReg(instr.Rd));
    const u32 rm_val = ARM_GetReg(instr.Rm);

    ARM_FlagsOut flags_out = {.Raw = cpu->CPSR.Flags};
    u32 alu_out;
    switch(instr.Opcode)
    {
    case 0: // ANDS
    case 8: // TST
        alu_out = rd_val & rm_val; break;
    case 1: // EORS
            // Fs in the chat for my man TEQ
        alu_out = rd_val ^ rm_val; break;
    case 2: // LSLS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_LSL(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 3: // LSRS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_LSR(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 4: // ASRS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_ASR(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 5: alu_out = ARM_ADC(rd_val, rm_val, cpu->CPSR.Carry, &flags_out); break; // ADCS
    case 6: alu_out = ARM_SBC_RSC(rd_val, rm_val, cpu->CPSR.Carry, &flags_out); break; // SBCS
    case 7: // RORS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_ROR(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 9: // RSBS (imm #0) AKA: NEGS
    case 10: // CMP
        alu_out = ARM_SUB_RSB(rd_val, rm_val, &flags_out); break;
    case 11: alu_out = ARM_ADD(rd_val, rm_val, &flags_out); break; // CMN
    case 12: alu_out = rd_val | rm_val; break; // ORRS
    case 13: alu_out = rd_val * rm_val; break; // MULS
    case 14: alu_out = rd_val & ~rm_val; break; // BICS
    case 15: alu_out = ~rm_val; break; // MVNS
    }

    // special multiply handling
    if (instr.Opcode == 13)
    {
        if (cpu->CPUID == ARM7ID)
        {
            int iterations = A7TDMI_NumBoothIters(rm_val, true);
            A7TDMI_ExecuteCycles(ARM7Cast, iterations);
            flags_out.Carry = flags_out.Carry; // TODO: Soon...
        }
        else A9ES_ExecuteCycles(ARM9Cast, 3);
    }
    else ARM_ExeCycles(1, 1);

    // all opcodes set flags
    flags_out.Negative = alu_out >> 31;
    flags_out.Zero = !alu_out;
    cpu->CPSR.Flags = flags_out.Raw;

    ARM_StepPC(cpu, true);

    // not TST, CMP, or CMN
    if ((instr.Opcode != 8) && (instr.Opcode != 10) && (instr.Opcode != 11))
        ARM_SetReg(instr.Rd, alu_out);
}

s8 THUMB9_DataProcReg_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_DataProcReg_Decode instr = {.Raw = instr_data.Raw};

    if (instr.Rm == reg) return len;
    if (((instr.Opcode != 9 /* NEGS */) && (instr.Opcode != 15 /* MVNS */)) && (instr.Rd == reg)) return len;

    if ((len > 1) && ((instr.Opcode == 8 /* TST */) || (instr.Opcode == 10 /* CMP */) || (instr.Opcode == 11 /* CMN */) // if it is an opcode that doesn't write Rd
    || ((instr.Opcode != 13 /* MULS */) && (instr.Rd != reg)))) // or if it isn't MULS and isn't writing Rd
        *retry = true;
    return 0;
}

union THUMB_DataProcHiReg_Decode
{
    u16 Raw;
    struct
    {
        u16 Rd : 3;
        u16 Rm : 4;
        u16 RdHi : 1;
        u16 Opcode : 2;
    };
    struct
    {
        u16 : 7;
        bool Link : 1;
    };
};

void THUMB_DataProcHiReg(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_DataProcHiReg_Decode instr = {.Raw = instr_data.Raw};

    const int rd = instr.Rd | (instr.RdHi << 3);

    u32 rd_val = 0;
    // MOV/CPY & BX/BLX dont use this reg
    if (instr.Opcode < 2) rd_val = ARM_GetReg(rd);

    u32 rm_val = ARM_GetReg(instr.Rm);

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    u32 alu_out;
    switch(instr.Opcode)
    {
    case 0: // ADD
    {
        ARM_FlagsOut flags_out;
        alu_out = ARM_ADD(rd_val, rm_val, &flags_out);
        break;
    }
    case 1: // CMP
    {
        ARM_FlagsOut flags_out;
        alu_out = ARM_SUB_RSB(rd_val, rm_val, &flags_out);

        // TODO: add stupid ARM7TDMI jank where it restores cpsr here

        // just set flags and return
        flags_out.Negative = alu_out >> 31;
        flags_out.Zero = !alu_out;
        cpu->CPSR.Flags = flags_out.Raw;
        return;
    }
    case 2: alu_out = rm_val; break; // MOV/CPY
    case 3: // BX/BLX
    {
        ARM_SetThumb(cpu, rm_val & 1);

        if (instr.Link)
        {
            // pc was stepped earlier so now i need to compensate with minus 4 oops.
            // (actually minus 3 since that gets the same result while also setting the lsb at the same time)
            ARM_SetReg(14, (ARM_GetReg(15) - 3));
        }
        ARM_SetReg(15, rm_val);
        // we handled all the logic here
        return;
    }
    }

    // only reached by ADD and MOV/CPY
    ARM_SetReg(rd, alu_out);
}

s8 THUMB9_DataProcHiReg_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_DataProcHiReg_Decode instr = {.Raw = instr_data.Raw};
    u8 rd = instr.Rd | (instr.RdHi << 3);

    if (instr.Rm == reg) return len;
    // MOV/CPY & BX/BLX dont use this reg
    if ((instr.Opcode < 2) && (rd == reg)) return len;

    if ((len > 1) && (((instr.Opcode == 3) && (!instr.Link && (14 != reg))) // bx / blx
    || (instr.Opcode == 2))) // cmp
        *retry = true; // dont test add/cpy; they're handled by the rd interlock test
    return 0;
}

union THUMB_AddPCSPRel_Decode
{
    u16 Raw;
    struct
    {
        u16 Imm8 : 8;
        u16 Rd : 3;
        bool SP : 1;
    };
};

void THUMB_AddPCSPRel(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_AddPCSPRel_Decode instr = {.Raw = instr_data.Raw};

    u32 alu_out;
    if (instr.SP)
        alu_out = ARM_GetReg(13);
    else // pc has bit 1 force cleared.
        alu_out = ARM_GetReg(15) & ~0b11;

    alu_out += instr.Imm8 * 4;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    ARM_SetReg(instr.Rd, alu_out);
}

s8 THUMB9_AddPCSPRel_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    const union THUMB_AddPCSPRel_Decode instr = {.Raw = instr_data.Raw};

    if (instr.SP && (13 == reg)) return len; // im not sure if this interlock can actually be triggered but it should work in theory?

    if ((len > 1) && (instr.Rd != reg)) *retry = true;
    return 0;
}

union THUMB_AdjustSP_Decode
{
    u16 Raw;
    struct
    {
        u16 Imm7 : 7;
        bool Sub : 1;
    };
};

void THUMB_AdjustSP(ARM* cpu, const ARM_Instr instr_data)
{
    const union THUMB_AdjustSP_Decode instr = {.Raw = instr_data.Raw};

    u32 alu_out = ARM_GetReg(13);

    if (instr.Sub) alu_out -= (instr.Imm7 * 4);
    else           alu_out += (instr.Imm7 * 4);

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1);

    ARM_SetReg(13, alu_out);
}

s8 THUMB9_AdjustSP_Interlocks(const ARM_Instr instr_data [[maybe_unused]], const s8 reg, const s8 len, const s8 len_c [[maybe_unused]], bool* retry)
{
    // im not sure if this interlock can actually be triggered but it should work in theory?
    if (13 == reg) return len;

    if (len > 1) *retry = true;
    return 0;
}
