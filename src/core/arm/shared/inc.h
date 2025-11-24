#pragma once

#include "../arm7/arm.h"
#include "../arm9/arm.h"




#define ARM9Cast ((struct ARM946ES*)cpu)
#define ARM7Cast ((struct ARM7TDMI*)cpu)

#define ARM_GetReg(reg) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_GetReg(ARM7Cast, (reg)) \
    : ARM9_GetReg(ARM9Cast, (reg)) \
)

#define ARM_SetReg(reg, val, interlock, interlock_c) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_SetReg(ARM7Cast, (reg), (val)) \
    : ARM9_SetReg(ARM9Cast, (reg), (val), (interlock), (interlock_c)) \
)

#define ARM_GetSPSR \
((cpu->CPUID == ARM7ID) \
    ? ARM7_GetSPSR(ARM7Cast) \
    : ARM9_GetSPSR(ARM9Cast) \
)

#define ARM_SetSPSR(psr) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_SetSPSR(ARM7Cast, (psr)) \
    : ARM9_SetSPSR(ARM9Cast, (psr)) \
)

#define ARM_RestoreSPSR \
ARM_SetCPSR(cpu, ARM_GetSPSR.Raw)

#define ARM_ExeCycles(exec7, exec9, mem9) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_ExecuteCycles(ARM7Cast, (exec7)) \
    : ARM9_ExecuteCycles(ARM9Cast, (exec9), (mem9)) \
)

#define ARM_GetVector \
((cpu->CPUID == ARM7ID) \
    ? 0x00000000 \
    : ARM9_GetExceptionBase(ARM9Cast) \
)

#define ARM_RaiseSWI \
((cpu->CPUID == ARM7ID) \
    ? ARM7_SoftwareInterrupt(cpu, (instr_data)) \
    : ARM9_SoftwareInterrupt(cpu, (instr_data)) \
)

#define ARM_RaiseUDF \
((cpu->CPUID == ARM7ID) \
    ? ARM7_UndefinedInstruction(cpu, (instr_data)) \
    : ARM9_UndefinedInstruction(cpu, (instr_data)) \
)

#define ARM_CanLoadInterwork \
((cpu->CPUID == ARM7ID) \
    ? false \
    : !((ARM9Cast)->CP15.CR.NoLoadTBit) \
)

[[nodiscard]] u32 ARM_ADD(const u32 rn_val, const u32 shifter_out, union ARM_FlagsOut* flags_out);
[[nodiscard]] u32 ARM_ADC(const u32 rn_val, const u32 shifter_out, const bool carry_in, union ARM_FlagsOut* flags_out);
[[nodiscard]] u32 ARM_SUB_RSB(const u32 a, const u32 b, union ARM_FlagsOut* flags_out);
[[nodiscard]] u32 ARM_SBC_RSC(const u32 a, const u32 b, const bool carry_in, union ARM_FlagsOut* flags_out);

[[nodiscard]] int ARM7_NumBoothIters(const u32 rs_val, const bool _signed);

[[nodiscard]] u32 ARM_LSL(u64 val, const u8 shift, bool* carry_out);
[[nodiscard]] u32 ARM_LSR(u64 val, const u8 shift, bool* carry_out);
[[nodiscard]] u32 ARM_ASR(u64 val, const u8 shift, bool* carry_out);
[[nodiscard]] u32 ARM_ROR(u32 val, const u8 shift, bool* carry_out);
