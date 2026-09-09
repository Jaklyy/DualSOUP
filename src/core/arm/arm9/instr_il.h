#pragma once

#include "core/utils.h"
#include "arm.h"




// stubs
[[nodiscard]] s8 A9ES_None_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_None_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
#define A9ES_UNIMPL_Interlocks A9ES_None_Interlocks
#define T9ES_UNIMPL_Interlocks T9ES_None_Interlocks



// ARM

// misc
[[nodiscard]] s8 A9ES_Uncond_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);

// data processing
[[nodiscard]] s8 A9ES_DataProc_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 A9ES_Mul_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 A9ES_CLZ_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry); 
[[nodiscard]] s8 A9ES_SatMath_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 A9ES_HalfwordMul_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);

// coprocessor
[[nodiscard]] s8 A9ES_MCR_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 A9ES_MRC_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
#define A9ES_MRC_Interlocks A9ES_None_Interlocks
[[nodiscard]] s8 A9ES_LDC_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);

// branch
[[nodiscard]] s8 A9ES_BranchExchange_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
#define A9ES_Branch_Interlocks A9ES_None_Interlocks
#define A9ES_BLXImm_Interlocks A9ES_None_Interlocks

// status
#define A9ES_MRS_Interlocks A9ES_None_Interlocks
s8 A9ES_MSR_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);

// load/store
s8 A9ES_LoadStore_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
s8 A9ES_LoadStoreMisc_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
s8 A9ES_LoadStoreMultiple_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
s8 A9ES_Swap_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);



// THUMB

// dataprocessing
[[nodiscard]] s8 T9ES_ShiftImm_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_AddSub_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
#define T9ES_MovsImm8_Interlocks T9ES_None_Interlocks
[[nodiscard]] s8 T9ES_DataProcImm8_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_DataProcReg_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_DataProcHiReg_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_AddPCSPRel_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_AdjustSP_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);

// branch
#define T9ES_BranchCond_Interlocks T9ES_None_Interlocks
[[nodiscard]] s8 T9ES_Branch_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);

// load/store
[[nodiscard]] s8 T9ES_LoadStoreReg_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_LoadStoreImm_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
#define T9ES_LoadStoreWordImm_Interlocks T9ES_LoadStoreImm_Interlocks
#define T9ES_LoadStoreHalfwordImm_Interlocks T9ES_LoadStoreImm_Interlocks
#define T9ES_LoadStoreByteImm_Interlocks T9ES_LoadStoreImm_Interlocks
#define T9ES_LoadPCRel_Interlocks T9ES_None_Interlocks
[[nodiscard]] s8 T9ES_LoadStoreSPRel_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_Push_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_Pop_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
[[nodiscard]] s8 T9ES_LoadStoreMultiple_Interlocks(const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c, bool* retry);
