#pragma once

#include "../../utils.h"
#include "arm.h"




// stubs
void ARM_UNIMPL(ARM* cpu, const ARM_Instr instr_data);
void THUMB_UNIMPL(ARM* cpu, const ARM_Instr instr_data);



// ARM

// data processing
void ARM_DataProc(ARM* cpu, const ARM_Instr instr_data);
void ARM_Mul(ARM* cpu, const ARM_Instr instr_data);
// v5+
void ARM_CLZ(ARM* cpu, const ARM_Instr instr_data); 
void ARM_SatMath(ARM* cpu, const ARM_Instr instr_data);
void ARM_HalfwordMul(ARM* cpu, const ARM_Instr instr_data);


// coprocessor
void ARM_MCR(ARM* cpu, const ARM_Instr instr_data);
void ARM_MRC(ARM* cpu, const ARM_Instr instr_data);
void ARM_LDC(ARM* cpu, const ARM_Instr instr_data);
// v5+
void ARM_MCR2(ARM* cpu, const ARM_Instr instr_data);
void ARM_MRC2(ARM* cpu, const ARM_Instr instr_data);

// branch
void ARM_Branch(ARM* cpu, const ARM_Instr instr_data);
void ARM_BLXImm(ARM* cpu, const ARM_Instr instr_data);
void ARM_BranchExchange(ARM* cpu, const ARM_Instr instr_data);

// status
void ARM_MRS(ARM* cpu, const ARM_Instr instr_data);
void ARM_MSR(ARM* cpu, const ARM_Instr instr_data);

// load/store
void ARM_LoadStore(ARM* cpu, const ARM_Instr instr_data);
void ARM_LoadStoreMisc(ARM* cpu, const ARM_Instr instr_data);
void ARM_LoadStoreMultiple(ARM* cpu, const ARM_Instr instr_data);
void ARM_Swap(ARM* cpu, const ARM_Instr instr_data);



// THUMB

// data processing
void THUMB_ShiftImm(ARM* cpu, const ARM_Instr instr_data);
void THUMB_AddSub(ARM* cpu, const ARM_Instr instr_data);
void THUMB_MovsImm8(ARM* cpu, const ARM_Instr instr_data);
void THUMB_DataProcImm8(ARM* cpu, const ARM_Instr instr_data);
void THUMB_DataProcReg(ARM* cpu, const ARM_Instr instr_data);
void THUMB_DataProcHiReg(ARM* cpu, const ARM_Instr instr_data);
void THUMB_AddPCSPRel(ARM* cpu, const ARM_Instr instr_data);
void THUMB_AdjustSP(ARM* cpu, const ARM_Instr instr_data);

// branch
void THUMB_BranchCond(ARM* cpu, const ARM_Instr instr_data);
void THUMB_Branch(ARM* cpu, const ARM_Instr instr_data);

// load/store
void THUMB_LoadStoreReg(ARM* cpu, const ARM_Instr instr_data);
void THUMB_LoadStoreWordImm(ARM* cpu, const ARM_Instr instr_data);
void THUMB_LoadStoreHalfwordImm(ARM* cpu, const ARM_Instr instr_data);
void THUMB_LoadStoreByteImm(ARM* cpu, const ARM_Instr instr_data);
void THUMB_LoadPCRel(ARM* cpu, const ARM_Instr instr_data);
void THUMB_LoadStoreSPRel(ARM* cpu, const ARM_Instr instr_data);
void THUMB_Push(ARM* cpu, const ARM_Instr instr_data);
void THUMB_Pop(ARM* cpu, const ARM_Instr instr_data);
void THUMB_LoadStoreMultiple(ARM* cpu, const ARM_Instr instr_data);
