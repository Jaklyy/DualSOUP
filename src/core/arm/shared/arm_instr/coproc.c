#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"




// coprocessor behavior is being hardcoded for our specific implementation.
// this is less than ideal but i didn't really feel like allocating 1.75 KiB for function pointers for each cpu for something i didn't personally need.

union ARM_MCR_MRC_Decode
{
    u32 Raw;
    struct
    {
        u32 CRm : 4;
        u32 : 1;
        u32 Op2 : 3;
        u32 Coproc : 4;
        u32 Rd : 4;
        u32 CRn : 4;
        u32 : 0;
        u32 Op1 : 3;
    };
};

void ARM9_MCR_15(struct ARM946ES* ARM9, const u8 Op1, const u8 CRn, const u8 CRm, const u8 Op2);
u32 ARM9_MRC_15(struct ARM946ES* ARM9, const u16 cmd);

void ARM_MCR(struct ARM* cpu, const u32 instr_data)
{
    const union ARM_MCR_MRC_Decode instr = {.Raw = instr_data};

    ARM_StepPC(cpu, false);
    u32 rd_val = ARM_GetReg(instr.Rd);

    if (cpu->CPUID == ARM7ID)
    {
        // this is valid for mrc at least.
        if (instr.Coproc == 14)
        {
            // uhhhhhh
        }
        else
        {
            /// UHHHHHHHHH
            // TODO: ARM7 UNDEFINED EXCEPTION???
            // TODO: ARM7 TIMINGS????
        }
    }
    else // ARM9ID
    {
        if (instr.Coproc == 15)
        {
            // this actually does stuff wow!
            // note: individual opcodes probably have different timings.
            u16 command = instr.Op1 << 11 | instr.CRn << 7 | instr.CRm << 3 | instr.Op2;
            ARM9_MCR_15((struct ARM946ES*)cpu, instr.Op1, instr.CRn, instr.CRm, instr.Op2);
        }
        else if (instr.Coproc == 14)
        {
            if (false) // enabled
            {
                // TODO???
            }
            else // disabled
            {
                // TODO: ARM9 TIMINGS
                ARM9_UndefinedInstruction(cpu, instr_data);
            }
        }
        else
        {
            LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: ABSENT COPROCESSOR WRITE: %i\n", instr.Coproc);
            // coprocessor undef takes 3 cycles vs the normal 1 cycle.
            ARM9_ExecuteCycles((struct ARM946ES*)cpu, 3, 1);
            ARM9_UndefinedInstruction(cpu, instr_data);
        }
    }
}

s8 ARM9_MCR_Interlocks(struct ARM946ES* ARM9, const u32 instr_data)
{
    const union ARM_MCR_MRC_Decode instr = {.Raw = instr_data};
    s8 stall = 0;
    // ARM9E-S docs specify it as needing data during it's decode stage...?
    // i dont think that's true here...?
    // i think it's literally just an STR but to a coprocessor instead of memory.
    ARM9_CheckInterlocks(ARM9, &stall, instr.Rd, 1, true);
    return stall;
}

void ARM_MRC(struct ARM* cpu, const u32 instr_data)
{
    const union ARM_MCR_MRC_Decode instr = {.Raw = instr_data};
    ARM_StepPC(cpu, false);

    u32 val;
    if (cpu->CPUID == ARM7ID)
    {
        if (instr.Coproc == 14)
        {
            if (false) // enabled???
            {
            }
            else
            {
                // open bus.
                // this is probably not always correct, but it's correct enough for now.
                // can dma change the open bus value?
                val = cpu->Instr[2];
                // TODO: ARM7 TIMINGS
            }
        }
        else
        {
            // TODO: ARM7 UNDEFINED EXCEPTION
            // TODO: ARM7 TIMINGS
            return;
        }
    }
    else // ARM9ID
    {
        if (instr.Coproc == 15)
        {
            ARM9_MRC_15((struct ARM946ES*)cpu, ARM_CoprocReg(instr.Op1, instr.CRn, instr.CRm, instr.Op2));
            // timings for MRC are always the same, no matter the command.
            if (instr.Rd == 15)
            {
                // flag update: takes longer due to needing to wait for the CPSR flag write.
                // speculation: this seems to be one of the few cases where the decode stage actually matters and effectively triggers an interlock.
                // CHECKME: this is semantically wrong i think?
                ARM9_ExecuteCycles((struct ARM946ES*)cpu, 3, 1);
            }
            else
            {
                // CHECKME:
                ARM9_ExecuteCycles((struct ARM946ES*)cpu, 1, 2);
            }
        }
        else if (instr.Coproc == 14)
        {
            if (false) // enabled
            {
                // TODO???
            }
            else // disabled
            {
                // TODO: ARM9 TIMINGS
                ARM9_UndefinedInstruction(cpu, instr_data);
            }
        }
        else
        {
            // TODO: ARM9 TIMINGS
            ARM9_UndefinedInstruction(cpu, instr_data);
        }
    }

    // encoding r15 results in the cpsr flags being set to the msb of the read.
    if (instr.Rd == 15)
    {
        cpu->CPSR.Flags = val >> 28;
    }
    else
    {
        // should be based off of ldr but it's not using port C...?
        ARM_SetReg(instr.Rd, val, 1, 1);
    }
}

s8 ARM9_MRC_Interlocks(struct ARM946ES* ARM9, const u32 instr_data)
{
    // it just dont
    return 0;
}

union ARM_LDC_Decode
{
    u32 Raw;
    struct
    {
        u32 WordOffset : 8;
        u32 Coproc : 4;
        u32 CRd : 4;
        u32 Rn : 4;
        u32 : 1; // idk
        bool W : 1; // wumbo
        bool N : 1; // numbo
        bool U : 1; // uumbo
        bool P : 1; // pumbo
    };
};

void ARM_LDC(struct ARM* cpu, const u32 instr_data)
{
    union ARM_LDC_Decode instr = {.Raw = instr_data};

    if (cpu->CPUID == ARM7ID)
    {
        // fart
    }
    else if (cpu->CPUID == ARM9ID)
    {
        if (instr.Coproc < 14) // fully absent coprocessors raise exceptions slower for some reason.
            ARM9_ExecuteCycles((struct ARM946ES*)cpu, 2, 1);

        ARM9_UndefinedInstruction(cpu, instr_data);
    }
}

s8 ARM9_LDC_Interlocks(struct ARM946ES* ARM9, const u32 instr_data)
{
    union ARM_LDC_Decode instr = {.Raw = instr_data};
    s8 stall = 0;

    ARM9_CheckInterlocks(ARM9, &stall, instr.Rn, 0, false);
    return stall;
}


// ARMv5 below here
// note: all of these instructions use the same interlock behavior as their standard counterparts


void ARM_MCR2(struct ARM* cpu, const u32 instr_data)
{
    // ARM9 CP15 treats MCR2 the same as MCR
    ARM_MCR(cpu, instr_data);
}

void ARM_MRC2(struct ARM* cpu, const u32 instr_data)
{
    // ARM9 CP15 treats MRC2 the same as MRC
    ARM_MRC(cpu, instr_data);
}
