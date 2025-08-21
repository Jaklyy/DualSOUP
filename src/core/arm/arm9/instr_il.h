#pragma once

#include "../../utils.h"
#include "arm.h"




// stubs
[[nodiscard]] s8 ARM9_None_Interlocks([[maybe_unused]] struct ARM946ES* ARM9, [[maybe_unused]] const u32 instr_data);
[[nodiscard]] s8 THUMB9_None_Interlocks([[maybe_unused]] struct ARM946ES* ARM9, [[maybe_unused]] const u32 instr_data);
[[maybe_unused]] s8 ARM9_UNIMPL_Interlocks([[maybe_unused]] struct ARM946ES* ARM9, [[maybe_unused]] const u32 instr_data);
[[maybe_unused]] s8 THUMB9_UNIMPL_Interlocks([[maybe_unused]] struct ARM946ES* ARM9, [[maybe_unused]] const u16 instr_data);

[[nodiscard]] s8 ARM9_DataProc_Interlocks(struct ARM946ES* ARM9, u32 instr_data);
[[nodiscard]] s8 ARM9_Mul_Interlocks(struct ARM946ES* ARM9, u32 instr_data);
[[nodiscard]] s8 ARM9_CLZ_Interlocks(struct ARM946ES* ARM9, const u32 instr_data); 
[[nodiscard]] s8 ARM9_SatMath_Interlocks(struct ARM946ES* ARM9, const u32 instr_data);
