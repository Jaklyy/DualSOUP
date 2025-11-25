#include "../../../utils.h"
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

void THUMB_ShiftImm(struct ARM* cpu, const struct ARM_Instr instr_data)
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
    ARM_ExeCycles(1, 1, 1);

    cpu->CPSR.Negative = rm_val >> 31;
    cpu->CPSR.Zero = !rm_val;
    cpu->CPSR.Carry = carry_out;

    ARM_SetReg(instr.Rd, rm_val, 0, 0);
}

s8 THUMB9_ShiftImm_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_ShiftImm_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    return stall;
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

void THUMB_AddSub(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_AddSub_Decode instr = {.Raw = instr_data.Raw};

    union ARM_FlagsOut flags_out;
    const u32 rn_val = ARM_GetReg(instr.Rn);
    // handle both imm3 and register variants here
    const u32 rm_val = (instr.Immediate) ? instr.Imm3 : ARM_GetReg(instr.Rm);

    u32 alu_out;
    if (instr.Subtract) alu_out = ARM_SUB_RSB(rn_val, rm_val, &flags_out);
    else                alu_out = ARM_ADD    (rn_val, rm_val, &flags_out);

    flags_out.Negative = rm_val >> 31;
    flags_out.Zero = !rm_val;

    // these instructions set flags
    cpu->CPSR.Flags = flags_out.Raw;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1, 1);

    ARM_SetReg(instr.Rd, alu_out, 0, 0);
}

s8 THUMB9_AddSub_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_AddSub_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    if (!instr.Immediate)
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    }

    return stall;
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

void THUMB_MovsImm8(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};

    // this instruction sets flags for some unfathomable reason.
    cpu->CPSR.Negative = 0; // the immediate is not signed. this cannot be set.
    cpu->CPSR.Zero = !instr.Imm8;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1, 1);

    ARM_SetReg(instr.Rd, instr.Imm8, 0, 0);
}

void THUMB_DataProcImm8(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};

    // mov is handled in a separate function since it really has no reason to share any logic.
    if (instr.Opcode == 0) unreachable();

    union ARM_FlagsOut flags_out;
    u32 rd_val = ARM_GetReg(instr.Rd);

    if (instr.Opcode == 2) // ADDS
    {
        rd_val = ARM_ADD(rd_val, instr.Imm8, &flags_out);
    }
    else // SUB/CMP
    {
        rd_val = ARM_SUB_RSB(rd_val, instr.Imm8, &flags_out);
    }

    flags_out.Negative = rd_val >> 31;
    flags_out.Zero = !rd_val;

    cpu->CPSR.Flags = flags_out.Raw;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1, 1);

    if (instr.Opcode != 1) // not CMP
    {
        ARM_SetReg(instr.Rd, rd_val, 0, 0);
    }
}

s8 THUMB9_DataProcImm8_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcImm8_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 0, false);
    return stall;
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

void THUMB_DataProcReg(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcReg_Decode instr = {.Raw = instr_data.Raw};

    // NEGS uses a value of 0.
    // MVNS doesn't fetch any value for rd
    const u32 rd_val = (((instr.Opcode == 9) || (instr.Opcode == 15)) ? 0 : ARM_GetReg(instr.Rd));
    const u32 rm_val = ARM_GetReg(instr.Rm);

    union ARM_FlagsOut flags_out = {.Raw = cpu->CPSR.Flags};
    u32 alu_out;
    switch(instr.Opcode)
    {
    case 0:  // ANDS
    case 8:  // TST
        alu_out = rd_val & rm_val; break;
    case 1:  // EORS
             // Fs in the chat for my man TEQ
        alu_out = rd_val ^ rm_val; break;
    case 2:  // LSLS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_LSL(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 3:  // LSRS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_LSR(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 4:  // ASRS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_ASR(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 5:  // ADCS
        alu_out = ARM_ADC(rd_val, rm_val, cpu->CPSR.Carry, &flags_out); break;
    case 6:  // SBCS
        alu_out = ARM_SBC_RSC(rd_val, rm_val, cpu->CPSR.Carry, &flags_out); break;
    case 7:  // RORS
    {
        bool carry_out = flags_out.Carry;
        alu_out = ARM_ROR(rd_val, rm_val, &carry_out); 
        flags_out.Carry = carry_out;
        break;
    }
    case 9:  // NEGS
    case 10: // CMP
        alu_out = ARM_SUB_RSB(rd_val, rm_val, &flags_out); break;
    case 11: // CMN
        alu_out = ARM_ADD(rd_val, rm_val, &flags_out); break;
    case 12: // ORR
        alu_out = rd_val | rm_val; break;
    case 13: // MULS
        alu_out = rd_val * rm_val; break;
    case 14: // BICS
        alu_out = rd_val & ~rm_val; break;
    case 15: // MVNS
        alu_out = ~rm_val; break;
    }

    // special multiply handling
    if (instr.Opcode == 13)
    {
        if (cpu->CPUID == ARM7ID)
        {
            int iterations = ARM7_NumBoothIters(rm_val, true);
            ARM7_ExecuteCycles(ARM7Cast, iterations + 1);
            flags_out.Carry = flags_out.Carry; // TODO: Soon...
        }
        else // ARM9ID
        {
            ARM9_ExecuteCycles(ARM9Cast, 4, 1);
        }
    }
    else
    {
        ARM_ExeCycles(1, 1, 1);
    }

    // all opcodes set flags
    flags_out.Negative = alu_out >> 31;
    flags_out.Zero = !alu_out;
    cpu->CPSR.Flags = flags_out.Raw;

    ARM_StepPC(cpu, true);

    // not TST, CMP, or CMN
    if (instr.Opcode != 8 && instr.Opcode != 10 && instr.Opcode != 11)
    {
        ARM_SetReg(instr.Rd, alu_out, 0, 0);
    }
}

s8 THUMB9_DataProcReg_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcReg_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    if (((instr.Opcode != 9) && (instr.Opcode != 15)))
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 0, false);
    }
    return stall;
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

void THUMB_DataProcHiReg(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcHiReg_Decode instr = {.Raw = instr_data.Raw};

    const int rd = instr.Rd | (instr.RdHi << 3);

    u32 rd_val;
    // MOV/CPY & BX/BLX dont use this reg
    if (instr.Opcode < 2)
    {
        rd_val = ARM_GetReg(rd);
    }

    u32 rm_val = ARM_GetReg(instr.Rm);

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1, 1);

    u32 alu_out;
    switch(instr.Opcode)
    {
    case 0: // ADD
    {
        union ARM_FlagsOut flags_out;
        alu_out = ARM_ADD(rd_val, rm_val, &flags_out);
        break;
    }
    case 1: // CMP
    {
        union ARM_FlagsOut flags_out;
        alu_out = ARM_SUB_RSB(rd_val, rm_val, &flags_out);

        // just set flags and return
        flags_out.Negative = alu_out >> 31;
        flags_out.Zero = !alu_out;
        cpu->CPSR.Flags = flags_out.Raw;
        return;
    }
    case 2: // MOV/CPY
        alu_out = rm_val; break;
    case 3: // BX/BLX
    {
        ARM_SetThumb(cpu, rm_val & 1);

        if (instr.Link)
        {
            // pc was stepped earlier so now i need to compensate with minus 4 oops.
            // (actually minus 3 since that gets the same result while also setting the lsb at the same time)
            ARM_SetReg(14, (ARM_GetReg(15) - 1), 0, 0);
        }
        ARM_SetReg(15, rm_val, 0, 0);
        // we handled all the logic here
        return;
    }
    }

    // only reached by ADD and MOV/CPY
    ARM_SetReg(rd, alu_out, 0, 0);
}

s8 THUMB9_DataProcHiReg_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_DataProcHiReg_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    // MOV/CPY & BX/BLX dont use this reg
    if (instr.Opcode < 2)
    {
        int rd = instr.Rd | (instr.RdHi << 3);
        ARM9_CheckInterlocks(ARM9, &stall, rd, 0, false);
    }

    return stall;
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

void THUMB_AddPCSPRel(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_AddPCSPRel_Decode instr = {.Raw = instr_data.Raw};

    u32 alu_out;
    if (instr.SP)
    {
        alu_out = ARM_GetReg(13);
    }
    else
    {
        // pc has bit 1 force cleared.
        alu_out = ARM_GetReg(15) & ~0b11;
    }
    alu_out += instr.Imm8 * 4;

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1, 1);

    ARM_SetReg(instr.Rd, alu_out, 0, 0);
}

s8 THUMB9_AddPCSPRel_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union THUMB_AddPCSPRel_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    if (instr.SP)
    {
        // im not sure if this interlock can actually be triggered but it should work in theory?
        ARM9_CheckInterlocks(ARM9, &stall, 13, 0, false);
    }

    return stall;
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

void THUMB_AdjustSP(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union THUMB_AdjustSP_Decode instr = {.Raw = instr_data.Raw};

    u32 alu_out = ARM_GetReg(13);

    if (instr.Sub) alu_out -= (instr.Imm7 * 4);
    else           alu_out += (instr.Imm7 * 4);

    ARM_StepPC(cpu, true);
    ARM_ExeCycles(1, 1, 1);

    ARM_SetReg(13, alu_out, 0, 0);
}

s8 THUMB9_AdjustSP_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    s8 stall = 0;
    // im not sure if this interlock can actually be triggered but it should work in theory?
    ARM9_CheckInterlocks(ARM9, &stall, 13, 0, false);

    return stall;
}
