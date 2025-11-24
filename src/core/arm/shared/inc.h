#pragma once

#include "../arm7/arm.h"
#include "../arm9/arm.h"




#define ARM_GetReg(reg) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_GetReg((struct ARM7TDMI*)cpu, (reg)) \
    : ARM9_GetReg((struct ARM946ES*)cpu, (reg)) \
)

#define ARM_SetReg(reg, val, interlock, interlock_c) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_SetReg((struct ARM7TDMI*)cpu, (reg), (val)) \
    : ARM9_SetReg((struct ARM946ES*)cpu, (reg), (val), (interlock), (interlock_c)) \
)

#define ARM_GetSPSR \
((cpu->CPUID == ARM7ID) \
    ? ARM7_GetSPSR((struct ARM7TDMI*)cpu) \
    : ARM9_GetSPSR((struct ARM946ES*)cpu) \
)

#define ARM_SetSPSR(psr) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_SetSPSR((struct ARM7TDMI*)cpu, (psr)) \
    : ARM9_SetSPSR((struct ARM946ES*)cpu, (psr)) \
)

#define ARM_RestoreSPSR \
ARM_SetCPSR(cpu, ARM_GetSPSR.Raw)

#define ARM_ExeCycles(exec7, exec9, mem9) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_ExecuteCycles((struct ARM7TDMI*)cpu, (exec7)) \
    : ARM9_ExecuteCycles((struct ARM946ES*)cpu, (exec9), (mem9)) \
)

#define ARM_GetVector \
((cpu->CPUID == ARM7ID) \
    ? 0x00000000 \
    : ARM9_GetExceptionBase((struct ARM946ES*)cpu) \
)

#define ARM_RaiseSWI \
(/*(cpu->CPUID == ARM7ID) \
    ? ARM7_SoftwareInterrupt(cpu, (instr_data)) \
    :*/ ARM9_SoftwareInterrupt(cpu, (instr_data)) \
)

#define ARM_RaiseUDF \
(/*(cpu->CPUID == ARM7ID) \
    ? ARM7_UndefinedInstruction(cpu, (instr_data)) \
    :*/ ARM9_UndefinedInstruction(cpu, (instr_data)) \
)

#define ARM_CanLoadInterwork \
((cpu->CPUID == ARM7ID) \
    ? false \
    : !(((struct ARM946ES*)cpu)->CP15.CR.NoLoadTBit) \
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
