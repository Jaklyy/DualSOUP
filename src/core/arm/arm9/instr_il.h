#pragma once

#include "../../utils.h"
#include "arm.h"




// stubs
[[nodiscard]] s8 ARM9_None_Interlocks([[maybe_unused]] struct ARM946ES* ARM9, [[maybe_unused]] const struct ARM_Instr instr_data);
[[nodiscard]] s8 THUMB9_None_Interlocks([[maybe_unused]] struct ARM946ES* ARM9, [[maybe_unused]] const struct ARM_Instr instr_data);
#define ARM9_UNIMPL_Interlocks ARM9_None_Interlocks
#define THUMB9_UNIMPL_Interlocks THUMB9_None_Interlocks



// ARM

// misc
[[nodiscard]] s8 ARM9_Uncond_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);

// data processing
[[nodiscard]] s8 ARM9_DataProc_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
[[nodiscard]] s8 ARM9_Mul_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
[[nodiscard]] s8 ARM9_CLZ_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data); 
[[nodiscard]] s8 ARM9_SatMath_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);

// coprocessor
[[nodiscard]] s8 ARM9_MCR_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
[[nodiscard]] s8 ARM9_MRC_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
[[nodiscard]] s8 ARM9_LDC_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);

// branch
[[nodiscard]] s8 ARM9_BranchExchange_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
#define ARM9_Branch_Interlocks ARM9_None_Interlocks
#define ARM9_BLXImm_Interlocks ARM9_None_Interlocks

// status
#define ARM9_MRS_Interlocks ARM9_None_Interlocks
s8 ARM9_MSR_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);



// THUMB

// dataprocessing
s8 THUMB9_ShiftImm_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
s8 THUMB9_AddSub_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
#define THUMB9_MovsImm8_Interlocks THUMB9_None_Interlocks
s8 THUMB9_DataProcImm8_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
s8 THUMB9_DataProcReg_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
s8 THUMB9_DataProcHiReg_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
s8 THUMB9_AddPCSPRel_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
s8 THUMB9_AdjustSP_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);

// branch
#define THUMB9_BranchCond_Interlocks THUMB9_None_Interlocks
s8 THUMB9_Branch_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data);
