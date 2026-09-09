#pragma once

#include "../arm7/arm.h"
#include "../arm9/arm.h"




#define ARM9Cast ((ARM946ES*)cpu)
#define ARM7Cast ((ARM7TDMI*)cpu)

#define ARM_GetReg(reg) \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_GetReg(ARM7Cast, (reg)) \
    : A9ES_GetReg(ARM9Cast, (reg)) \
)

#define ARM_SetReg(reg, val) \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_SetReg(ARM7Cast, (reg), (val)) \
    : A9ES_SetReg(ARM9Cast, (reg), (val)) \
)

#define ARM_GetSPSR \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_GetSPSR(ARM7Cast) \
    : A9ES_GetSPSR(ARM9Cast) \
)

#define ARM_SetSPSR(psr) \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_SetSPSR(ARM7Cast, (psr)) \
    : A9ES_SetSPSR(ARM9Cast, (psr)) \
)

#define ARM_RestoreSPSR \
ARM_SetCPSR(cpu, ARM_GetSPSR.Raw)

#define ARM_ExeCycles(exec7, exec9) \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_ExecuteCycles(ARM7Cast, (exec7)-1) \
    : A9ES_ExecuteCycles(ARM9Cast, (exec9)-1) \
)

#define ARM_GetVector \
((cpu->CPUID == ARM7ID) \
    ? 0x00000000 \
    : A946_GetExceptionBase(ARM9Cast) \
)

#define ARM_RaiseSVC \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_SupervisorCall(cpu, (instr_data)) \
    : A9ES_SupervisorCall(cpu, (instr_data)) \
)

#define ARM_RaiseUDF \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_UndefinedInstruction(cpu, (instr_data)) \
    : A9ES_UndefinedInstruction(cpu, (instr_data)) \
)

#define ARM_CanLoadInterwork \
((cpu->CPUID == ARM7ID) \
    ? false \
    : !((ARM9Cast)->CP15.CR.NoLoadTBit) \
)

#define ARM_FlushPipeline \
((cpu->CPUID == ARM7ID) \
    ? A7TDMI_FlushPipeline(ARM7Cast) \
    : A9ES_FlushPipeline(ARM9Cast) \
)

[[nodiscard]] u32 ARM_ADD(const u32 rn_val, const u32 shifter_out, ARM_FlagsOut* flags_out);
[[nodiscard]] u32 ARM_ADC(const u32 rn_val, const u32 shifter_out, const bool carry_in, ARM_FlagsOut* flags_out);
[[nodiscard]] u32 ARM_SUB_RSB(const u32 a, const u32 b, ARM_FlagsOut* flags_out);
[[nodiscard]] u32 ARM_SBC_RSC(const u32 a, const u32 b, const bool carry_in, ARM_FlagsOut* flags_out);

[[nodiscard]] u8 A7TDMI_NumBoothIters(const u32 rs_val, const bool signedcheck);

[[nodiscard]] u32 ARM_LSL(u64 val, const u8 shift, bool* carry_out);
[[nodiscard]] u32 ARM_LSR(u64 val, const u8 shift, bool* carry_out);
[[nodiscard]] u32 ARM_ASR(u64 val, const u8 shift, bool* carry_out);
[[nodiscard]] u32 ARM_ROR(u32 val, const u8 shift, bool* carry_out);

void ARM_STR(ARM* cpu, u32 addr, u8 rd, bool priv, u8 rn, u32 wbaddr, u32 baserestore, bool writeback, ARM_DataWidth size);
void ARM_LDR(ARM* cpu, u32 addr, u8 rd, bool priv, u8 rn, u32 wbaddr, u32 baserestore, bool writeback, ARM_DataWidth size, bool signext);

void ARM_STM(ARM* cpu, u32 addr, u16 rlist, u32 wbaddr, u32 baserestore, u8 rn, bool writeback, bool special);
void ARM_LDM(ARM* cpu, u32 addr, u16 rlist, u32 wbaddr, u32 baserestore, u8 rn, bool writeback, bool special);
