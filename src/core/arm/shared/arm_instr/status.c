#include "../../../utils.h"
#include "../arm.h"
#include "../inc.h"




union ARM_StatusReg_Decode
{
    u32 Raw;
    struct
    {
        u32 Rm : 4; // (SBZ MRS)
        u32 : 8;
        u32 Rd : 4; // (SBO MSR)
        u32 : 6;
        bool UseSPSR : 1;
    };
    struct
    {
        u32 Imm : 8;
        u32 RotateImm : 4;
        u32 : 4; // Rd
        // field mask
        bool Control : 1;
        bool Extension : 1;
        bool Status : 1;
        bool Flags : 1;
        u32 : 5;
        bool UseImmediate : 1;
    };
};

void ARM_MRS(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_StatusReg_Decode instr = {.Raw = instr_data.Raw};

    u32 psr = ((instr.UseSPSR) ? ARM_GetSPSR.Raw : cpu->CPSR.Raw);

    ARM_StepPC(cpu, false);
    // TODO: double check timings.
    ARM_ExeCycles(1, 1, 2);

    // r15 writeback doesn't work on arm9
    if ((instr.Rd != 15) || (cpu->CPUID != ARM9ID))
    {
        ARM_SetReg(instr.Rd, psr, 0, 0);
    }
}

void ARM_MSR(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    const union ARM_StatusReg_Decode instr = {.Raw = instr_data.Raw};

    u32 input;

    if (instr.UseImmediate)
    {
        bool* dummy = 0; // we dont care about the carry out...
        input = ARM_ROR(instr.Imm, instr.RotateImm, dummy);
    }
    else
    {
        input = ARM_GetReg(instr.Rm);
    }

    // determine which bits actually exist and are changeable.
    u32 updatemask;
    if (cpu->CPUID == ARM7ID)
    {
        updatemask = 0xF00000EF;
    }
    else //if (cpu->CPUID == ARM9ID)
    {
        updatemask = 0xF80000EF;
    }

    // if we're in user mode, most bits are locked out from changes.
    if (!cpu->Privileged)
    {
        // note: armv6 adds a bunch of misc junk in the status and extension fields for w/e reason
        updatemask &= 0xF80F0200;
    }

    // mask out bits were aren't trying to change.
    if (!instr.Control  ) updatemask &= ~0x000000FF;
    if (!instr.Extension) updatemask &= ~0x0000FF00;
    if (!instr.Status   ) updatemask &= ~0x00FF0000;
    if (!instr.Flags    ) updatemask &= ~0xFF000000;


    // grab target psr
    u32 psr = ((instr.UseSPSR) ? ARM_GetSPSR.Raw : cpu->CPSR.Raw);
    u32 oldpsr = psr;

    // actually mask out bits
    psr = (psr & ~updatemask) | (input & updatemask);

    // check if thumb bit was messed with (dont ask me why arm didn't just mask this bit out...)
    if ((instr.UseSPSR) && ((psr ^ oldpsr) & 0x20))
    {
        if (cpu->CPUID == ARM9ID)
        {
            // this processor is mostly sane i think, so we'll implement it. Still gonna log it though.
            // TODO: does this mess up the coprocessor pipeline?

            LogPrint(LOG_ODD | LOG_CPUID, "ARM9: MSR THUMB bit write?\n");

            // compensate for our hacky prefetch handling
            for (int i = 1; i < 3; i++)
            {
                if (cpu->Instr[i].Aborted)
                {
                    if (psr & 0x20)
                        cpu->Instr[i].Raw = 0xBE00;
                    else
                        cpu->Instr[i].Raw = 0xE1200070;

                }
            }
        }
        else LogPrint(LOG_UNIMP | LOG_CPUID, "ARM%i: MSR THUMB BIT WRITE AAAAAAAA SCARY\n", CPUIDtoCPUNum);
    }

    ARM_StepPC(cpu, false);

    // handle timings... here we go....
    if ((cpu->CPUID == ARM9ID) && (instr.Control || instr.Extension || instr.Status || instr.UseSPSR))
    {
        // yes that is actually how it works.
        // no i dont know why it cares about the extension or status bits?
        // CHECKME: ...it might also care about none set actually...?
        ARM9_ExecuteCycles(ARM9Cast, 3, 1);
    }
    else
    {
        ARM_ExeCycles(1, 1, 1);
    }

    ((instr.UseSPSR) ? ARM_SetSPSR((union ARM_PSR){.Raw = psr}) : ARM_SetCPSR(cpu, psr));
}

s8 ARM9_MSR_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const union ARM_StatusReg_Decode instr = {.Raw = instr_data.Raw};
    s8 stall = 0;

    // only check for register variant
    if (!instr.UseImmediate)
    {
        ARM9_CheckInterlocks(ARM9, &stall, instr.Rm, 0, false);
    }

    return stall;
}
