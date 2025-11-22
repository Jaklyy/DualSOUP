#pragma once

#include "../../utils.h"
#include "arm.h"




// stubs
void ARM_UNIMPL(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_UNIMPL(struct ARM* cpu, const struct ARM_Instr instr_data);



// ARM

// data processing
void ARM_DataProc(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_Mul(struct ARM* cpu, const struct ARM_Instr instr_data);
// v5+
void ARM_CLZ(struct ARM* cpu, const struct ARM_Instr instr_data); 
void ARM_SatMath(struct ARM* cpu, const struct ARM_Instr instr_data);


// coprocessor
void ARM_MCR(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_MRC(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_LDC(struct ARM* cpu, const struct ARM_Instr instr_data);
// v5+
void ARM_MCR2(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_MRC2(struct ARM* cpu, const struct ARM_Instr instr_data);

// branch
void ARM_Branch(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_BLXImm(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_BranchExchange(struct ARM* cpu, const struct ARM_Instr instr_data);

// status
void ARM_MRS(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_MSR(struct ARM* cpu, const struct ARM_Instr instr_data);

// load/store
void ARM_LoadStore(struct ARM* cpu, const struct ARM_Instr instr_data);
void ARM_LoadStoreMisc(struct ARM* cpu, const struct ARM_Instr instr_data);



// THUMB

// data processing
void THUMB_ShiftImm(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_AddSub(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_MovsImm8(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_DataProcImm8(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_DataProcReg(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_DataProcHiReg(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_AddPCSPRel(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_AdjustSP(struct ARM* cpu, const struct ARM_Instr instr_data);

// branch
void THUMB_BranchCond(struct ARM* cpu, const struct ARM_Instr instr_data);
void THUMB_Branch(struct ARM* cpu, const struct ARM_Instr instr_data);
