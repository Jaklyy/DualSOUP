#pragma once

#include "../../utils.h"
#include "arm.h"




// stubs
void ARM_UNIMPL(struct ARM* cpu, const u32 instr_data);
void THUMB_UNIMPL(struct ARM* cpu, const u16 instr_data);

// data processing
void ARM_DataProc(struct ARM* cpu, const u32 instr_data);
void ARM_Mul(struct ARM* cpu, const u32 instr_data);
// v5+
void ARM_CLZ(struct ARM* cpu, const u32 instr_data); 
void ARM_SatMath(struct ARM* cpu, const u32 instr_data);
