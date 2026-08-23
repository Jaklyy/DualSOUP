#pragma once

#include "../../utils.h"
#include "arm.h"




// stubs
[[nodiscard]] s8 A9ES_None_Interlocks([[maybe_unused]] ARM946ES* a9es, [[maybe_unused]] const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_None_Interlocks([[maybe_unused]] ARM946ES* a9es, [[maybe_unused]] const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
#define A9ES_UNIMPL_Interlocks A9ES_None_Interlocks
#define T9ES_UNIMPL_Interlocks T9ES_None_Interlocks



// ARM

// misc
[[nodiscard]] s8 A9ES_Uncond_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);

// data processing
[[nodiscard]] s8 A9ES_DataProc_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 A9ES_Mul_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 A9ES_CLZ_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c); 
[[nodiscard]] s8 A9ES_SatMath_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 A9ES_HalfwordMul_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);

// coprocessor
[[nodiscard]] s8 A9ES_MCR_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 A9ES_MRC_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
#define A9ES_MRC_Interlocks A9ES_None_Interlocks
[[nodiscard]] s8 A9ES_LDC_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);

// branch
[[nodiscard]] s8 A9ES_BranchExchange_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
#define A9ES_Branch_Interlocks A9ES_None_Interlocks
#define A9ES_BLXImm_Interlocks A9ES_None_Interlocks

// status
#define A9ES_MRS_Interlocks A9ES_None_Interlocks
s8 A9ES_MSR_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);

// load/store
s8 A9ES_LoadStore_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
s8 A9ES_LoadStoreMisc_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
s8 A9ES_LoadStoreMultiple_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
s8 A9ES_Swap_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);



// THUMB

// dataprocessing
[[nodiscard]] s8 T9ES_ShiftImm_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_AddSub_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
#define T9ES_MovsImm8_Interlocks T9ES_None_Interlocks
[[nodiscard]] s8 T9ES_DataProcImm8_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_DataProcReg_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_DataProcHiReg_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_AddPCSPRel_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_AdjustSP_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);

// branch
#define T9ES_BranchCond_Interlocks T9ES_None_Interlocks
[[nodiscard]] s8 T9ES_Branch_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);

// load/store
[[nodiscard]] s8 T9ES_LoadStoreReg_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_LoadStoreImm_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
#define T9ES_LoadStoreWordImm_Interlocks T9ES_LoadStoreImm_Interlocks
#define T9ES_LoadStoreHalfwordImm_Interlocks T9ES_LoadStoreImm_Interlocks
#define T9ES_LoadStoreByteImm_Interlocks T9ES_LoadStoreImm_Interlocks
#define T9ES_LoadPCRel_Interlocks T9ES_None_Interlocks
[[nodiscard]] s8 T9ES_LoadStoreSPRel_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_Push_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_Pop_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
[[nodiscard]] s8 T9ES_LoadStoreMultiple_Interlocks(ARM946ES* a9es, const ARM_Instr instr_data, const s8 reg, const s8 len, const s8 len_c);
