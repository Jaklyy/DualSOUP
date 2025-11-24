#include "arm.h"




#define cpu ((struct ARM*)ARM7)

void ARM7_Reset(struct ARM7TDMI* ARM7)
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
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_SWI);

    // one can only imagine what pc would be here... probably depends on when the current instruction got interrupted?
    u32 oldpc = cpu->PC;
    cpu->LR = oldpc;
    ARM7_SetSPSR(ARM7, oldcpsr);

    // set cpsr bits
    // flag bits dont seem to be mentioned anywhere?
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->CpuSleeping = 0;

    ARM7_SetPC(ARM7, ARMVector_RST);
}

void ARM7_RaiseUDF(struct ARM* ARM, const struct ARM_Instr instr_data, const int cycles)
{
    struct ARM7TDMI* ARM7 = (struct ARM7TDMI*)ARM;

    if (cpu->CPSR.Thumb)
        LogPrint(LOG_ARM9 | LOG_EXCEP, "THUMB7 - UNDEF INSTR: %04X @ %08X\n", instr_data.Raw, cpu->PC);
    else
        LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM7 - UNDEF INSTR: %08X @ %08X\n", instr_data.Raw, cpu->PC);

    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM7_ExecuteCycles(ARM7, cycles);

    ARM_SetMode(cpu, ARMMode_UND);

    cpu->LR = oldpc;
    ARM7_SetSPSR(ARM7, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    ARM7_SetPC(ARM7, ARMVector_UND);
}

void ARM7_UndefinedInstruction(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM7_RaiseUDF(ARM, instr_data, 1);
}

void THUMB7_UndefinedInstruction(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM7_RaiseUDF(ARM, instr_data, 1);
}

void ARM7_SoftwareInterrupt(struct ARM* ARM, [[maybe_unused]] const struct ARM_Instr instr_data)
{
    // TODO: could add a print here for logging software interrupts that gets fired.
    struct ARM7TDMI* ARM7 = (struct ARM7TDMI*)ARM;

    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM7_ExecuteCycles(ARM7, 1);

    ARM_SetMode(cpu, ARMMode_SWI);

    cpu->LR = oldpc;
    ARM7_SetSPSR(ARM7, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    ARM7_SetPC(ARM7, ARMVector_SWI);
}

void THUMB7_SoftwareInterrupt(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM7_SoftwareInterrupt(ARM, instr_data);
}

// TODO: data/prefetch aborts?

void ARM7_InterruptRequest(struct ARM7TDMI* ARM7)
{
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM7_ExecuteCycles(ARM7, 1);

    ARM_SetMode(cpu, ARMMode_IRQ);

    cpu->LR = oldpc;
    ARM7_SetSPSR(ARM7, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;

    cpu->CpuSleeping = 0;

    ARM7_SetPC(ARM7, ARMVector_IRQ);
}

void ARM7_FastInterruptRequest(struct ARM7TDMI* ARM7)
{
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM7_ExecuteCycles(ARM7, 1);

    ARM_SetMode(cpu, ARMMode_FIQ);

    cpu->LR = oldpc;
    ARM7_SetSPSR(ARM7, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->CpuSleeping = 0;

    ARM7_SetPC(ARM7, ARMVector_FIQ);
}

#undef cpu
