#include "arm.h"




#define cpu (&a7tdmi->ARM)

void A7TDMI_Reset(ARM7TDMI* a7tdmi)
{
    // according to docs reset requires:
    // min 2 cycles lo
    // then it takes 2 cycles to actually begin fetching again
    cpu->Timestamp += 2 + 2;

    // NOTE: apparently while nRESET signal is low the arm7
    // "continues to increment the address bus
    // as if still fetching word or halfword instructions"
    // while indicating internal cycles on nMREQ and SEQ signals

    // arm7 stores pc and cpsr on reset
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_SVC);

    // one can only imagine what pc would be here... probably depends on when the current instruction got interrupted?
    u32 oldpc = cpu->PC;
    cpu->LR = oldpc;
    A7TDMI_SetSPSR(a7tdmi, oldcpsr);

    // set cpsr bits
    // flag bits dont seem to be mentioned anywhere?
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    A7TDMI_SetPC(a7tdmi, ARMVector_RST);
}

void A7TDMI_RaiseUDF(ARM* ARM, const ARM_Instr instr_data, const int cycles)
{
    ARM7TDMI* a7tdmi = (ARM7TDMI*)ARM;

    if (cpu->CPSR.Thumb)
        LogPrint(LOG_ARM9 | LOG_EXCEP, "THUMB7 - UNDEF INSTR: %04X @ %08X\n", instr_data.Raw, cpu->PC);
    else
        LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM7 - UNDEF INSTR: %08X @ %08X\n", instr_data.Raw, cpu->PC);

    //CrashSpectacularly("FARK\n");
    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    A7TDMI_ExecuteCycles(a7tdmi, cycles-1);

    ARM_SetMode(cpu, ARMMode_UND);

    cpu->LR = oldpc;
    A7TDMI_SetSPSR(a7tdmi, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    A7TDMI_SetPC(a7tdmi, ARMVector_UND);
}

void A7TDMI_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data)
{
    A7TDMI_RaiseUDF(ARM, instr_data, 1);
}

void T7TDMI_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data)
{
    A7TDMI_RaiseUDF(ARM, instr_data, 1);
}

void A7TDMI_SupervisorCall(ARM* ARM, [[maybe_unused]] const ARM_Instr instr_data)
{
    // TODO: could add a print here for logging software interrupts that gets fired.
    ARM7TDMI* a7tdmi = (ARM7TDMI*)ARM;

    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_SVC);

    cpu->LR = oldpc;
    A7TDMI_SetSPSR(a7tdmi, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    A7TDMI_SetPC(a7tdmi, ARMVector_SVC);
}

void T7TDMI_SupervisorCall(ARM* ARM, const ARM_Instr instr_data)
{
    A7TDMI_SupervisorCall(ARM, instr_data);
}

// TODO: data/prefetch aborts?

void A7TDMI_InterruptRequest(ARM7TDMI* a7tdmi)
{
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_IRQ);

    cpu->LR = oldpc;
    A7TDMI_SetSPSR(a7tdmi, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;

    cpu->CpuSleeping = 0;

    A7TDMI_SetPC(a7tdmi, ARMVector_IRQ);
}

void A7TDMI_FastInterruptRequest(ARM7TDMI* a7tdmi)
{
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_FIQ);

    cpu->LR = oldpc;
    A7TDMI_SetSPSR(a7tdmi, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->CpuSleeping = 0;

    A7TDMI_SetPC(a7tdmi, ARMVector_FIQ);
}

#undef cpu
