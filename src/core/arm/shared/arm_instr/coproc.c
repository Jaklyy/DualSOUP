#include "core/utils.h"
#include "../../arm9/arm.h"
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
        u32 : 1;
        u32 Op1 : 3;
    };
};

void ARM_MCR(ARM* cpu, const ARM_Instr instr_data)
{
    const union ARM_MCR_MRC_Decode instr = {.Raw = instr_data.Raw};

    ARM_StepPC(cpu, false);
    u32 rd_val = ARM_GetReg(instr.Rd);

    if (cpu->CPUID == ARM7ID)
    {
        // this is valid for mrc at least.
        if (instr.Coproc == 14) // debug
        {
            // uhhhhhh
            LogPrint(LOG_ARM7 | LOG_EXCEP, "ARM7: MCR?!\n");
        }
        else // absent
        {
            /// UHHHHHHHHH
            A7TDMI_RaiseUDF(cpu, instr_data, 1); // idk cycle counts.
            // TODO: ARM7 UNDEFINED EXCEPTION???
            // TODO: ARM7 TIMINGS????
        }
    }
    else // ARM9ID
    {
        if (instr.Coproc == 15) // system control
        {
            if (instr_data.CoprocPriv != cpu->Privileged) 
            {
                LogPrint(LOG_ARM9 | LOG_BUG, "ARM9 ERRATA TRIGGERED: MCR COPROC 15 USING STALE PRIVILEGES!\n");
            }

            if (instr_data.CoprocPriv) // requires privileged mode (this is different than the normal arm privilege check)
            {
                // this actually does stuff wow!
                // note: individual opcodes probably have different timings.
                A946_CP15Write(ARM9Cast, ARM_CoprocReg(instr.Op1, instr.CRn, instr.CRm, instr.Op2), rd_val);
            }
            else
            {
                // user mode; raise udf
                // present coprocessors seemingly take 2 cycles longer to raise udf
                LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: USER MODE MCR COPROC 15!\n");
                A9ES_RaiseUDF(cpu, instr_data, 3);
            }
        }
        else if (instr.Coproc == 14) // debug coprocessor
        {
            if (false) // enabled
            {
                // TODO???
            }
            else // disabled
            {
                // present coprocessors seemingly take 2 cycles longer to raise udf
                LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: MCR COPROC 14!\n");
                A9ES_RaiseUDF(cpu, instr_data, 3);
            }
        }
        else // absent
        {
            // absent coprocessors takes 3 cycles longer to raise undefined
            LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: MCR ABSENT COPROC %"PRIu32"!\n", instr.Coproc);
            A9ES_RaiseUDF(cpu, instr_data, 4);
        }
    }
}

s8 A9ES_MCR_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len [[maybe_unused]], const s8 len_c)
{
    const union ARM_MCR_MRC_Decode instr = {.Raw = instr_data.Raw};
    // ARM9E-S docs specify it as needing data during it's decode stage...?
    // i dont think that's true...? (does it mean coprocessor decode stage?)
    // i think it's literally just an STR but to a coprocessor instead of memory.

    if (reg == instr.Rd) return len_c-1;

    return 0;
}

void ARM_MRC(ARM* cpu, const ARM_Instr instr_data)
{
    const union ARM_MCR_MRC_Decode instr = {.Raw = instr_data.Raw};
    ARM_StepPC(cpu, false);

    u32 val;
    if (cpu->CPUID == ARM7ID)
    {
        if (instr.Coproc == 14) // debug coprocessor
        {
            if (false) // enabled???
            {
            }
            else
            {
                // open bus.
                // this is probably not always correct, but it's correct enough for now.
                // can dma change the open bus value?
                val = cpu->Instr[2].Raw;
                // TODO: ARM7 TIMINGS
                LogPrint(LOG_ARM7 | LOG_ODD, "ARM7: MRC COPROC 14!\n");
            }
        }
        else // absent
        {
            // TODO: ARM7 UNDEFINED EXCEPTION
            // TODO: ARM7 TIMINGS
            A7TDMI_RaiseUDF(cpu, instr_data, 1); // idk cycle counts.
            LogPrint(LOG_ARM7 | LOG_EXCEP, "ARM7: MRC ABSENT COPROC %"PRIu32"!\n", instr.Coproc);
            return;
        }
    }
    else // ARM9ID
    {
        if (instr.Coproc == 15) // system control coprocessor
        {
            if (instr_data.CoprocPriv != cpu->Privileged) 
                LogPrint(LOG_ARM9 | LOG_BUG, "ARM9 ERRATA TRIGGERED: MRC COPROC 15 USING STALE PRIVILEGES!\n");

            if (instr_data.CoprocPriv) // requires privileged mode (this is different than the normal arm privilege check)
            {
                val = A946_CP15Read(ARM9Cast, ARM_CoprocReg(instr.Op1, instr.CRn, instr.CRm, instr.Op2));
                // timings for MRC are always the same, no matter the command.
                if (instr.Rd == 15)
                {
                    // flag update: takes longer due to needing to wait for the CPSR flag write.
                    // speculation: this seems to be one of the few cases where the decode stage actually matters for timings and effectively triggers an interlock.
                    A9ES_ExecuteCycles(ARM9Cast, 2);
                }
                else
                {
                    // should be based off of ldr but it doesn't cause extra delay for reads via port C...?
                    s8 interlock = A9ES_DecodeInterlocks(ARM9Cast, false, instr.Rd, 1, 1, nullptr);
                    // CHECKME: memory 2?
                    A9ES_ExecuteCycles(ARM9Cast, 1+interlock);
                }
            }
            else
            {
                // user mode; raise udf
                // present coprocessors seemingly take 2 cycles longer to raise udf
                LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: USER MODE MRC COPROC 15!\n");
                return A9ES_RaiseUDF(cpu, instr_data, 2);
            }
        }
        else if (instr.Coproc == 14) // debug coprocessor
        {
            if (false) // enabled
            {
                // TODO???
            }
            else // disabled
            {
                // present coprocessors seemingly take 2 cycles longer to raise udf
                LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: MRC COPROC 14!\n");
                return A9ES_RaiseUDF(cpu, instr_data, 3);
            }
        }
        else // absent
        {
            // absent coprocessors takes 3 cycles longer to raise undefined
            LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9: MRC ABSENT COPROC %"PRIu32"!\n", instr.Coproc);
            return A9ES_RaiseUDF(cpu, instr_data, 4);
        }
    }

    // encoding r15 results in the cpsr flags being set to the msb of the read.
    if (instr.Rd == 15) cpu->CPSR.Flags = val >> 28;
    else ARM_SetReg(instr.Rd, val);
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

void ARM_LDC(ARM* cpu, const ARM_Instr instr_data)
{
    union ARM_LDC_Decode instr = {.Raw = instr_data.Raw};

    if (cpu->CPUID == ARM7ID)
    {
        // fart
    }
    else if (cpu->CPUID == ARM9ID)
    {
        if (instr.Coproc < 14) // fully absent coprocessors raise exceptions slower for some reason.
            A9ES_ExecuteCycles(ARM9Cast, 2);

        A9ES_UndefinedInstruction(cpu, instr_data);
    }
}

s8 A9ES_LDC_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c [[maybe_unused]])
{
    union ARM_LDC_Decode instr = {.Raw = instr_data.Raw};

    if (reg == instr.Rn) return len;

    return 0;
}

// ARMv5 below here
// note: all of these instructions use the same interlock behavior as their standard counterparts

void ARM_MCR2(ARM* cpu, const ARM_Instr instr_data)
{
    // ARM9 CP15 treats MCR2 the same as MCR
    ARM_MCR(cpu, instr_data);
}

void ARM_MRC2(ARM* cpu, const ARM_Instr instr_data)
{
    // ARM9 CP15 treats MRC2 the same as MRC
    ARM_MRC(cpu, instr_data);
}
