#pragma once

#include <stddef.h>
#include "core/utils.h"
#include "../shared/arm.h"



// NDS model: ARM7TDMI (unknown revision?) (i'm speculating its Rev 4, but its unconfirmed)
// GBA model: ARM7TDMI Rev 3A

/*
    name decodes as:
    7: Orange
    T: Thumb
    D: Something debugging related
    M: Fast Multiplier (it was so good they removed it from the ARM946E-S)
    I: Debugging but different
*/
typedef enum : u8
{
    A7TDMIDataCB_LoadSingle,
    A7TDMIDataCB_StoreSingle,
    A7TDMIDataCB_LoadMultiple,
    A7TDMIDataCB_StoreMultiple,
    A7TDMIDataCB_SwapLoad,
    A7TDMIDataCB_SwapStore,
} A7TDMI_DataCB;

typedef struct
{
    union {
        u32 RData[16];
        u32 WrData[16];
    };
    u32 Addr;
    //u32 BaseRestore;
    u16 RListOrig;
    u16 RListRem;
    u8 RBase;
    //u8 DataPtr; // used by biu and cache streaming
    //bool DataAbort;
    u8 NumFetch;
    u8 NumFetchCompleted;
    ARM_DataWidth Size;
    union {
        bool Special; // ldm/stm
        bool SignExt; // ldr
    };
    bool Priv;
    A7TDMI_DataCB DataCB;
} A7TDMI_PostMem;

typedef struct
{
    ARM ARM;
    A7TDMI_PostMem PostMem;
    bool BusGo;
} ARM7TDMI;

// ensure casting between the two types works as expected
static_assert(offsetof(ARM7TDMI, ARM) == 0);

extern void (*A7TDMI_InstructionLUT[0x1000])(ARM*, const ARM_Instr);
extern void (*T7TDMI_InstructionLUT[64])(ARM*, const ARM_Instr);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void A7TDMI_Init(ARM7TDMI* a7tdmi, Console* console);

// arm7 handler entrypoint
void A7TDMI_Run(ARM7TDMI* a7tdmi, timestamp now);

// special exceptions
void A7TDMI_Reset(ARM7TDMI* a7tdmi);
void A7TDMI_InterruptRequest(ARM7TDMI* a7tdmi);
// only used by debug hardware
void A7TDMI_FastInterruptRequest(ARM7TDMI* a7tdmi);

void A7TDMI_RaiseUDF(ARM* ARM, const ARM_Instr instr_data, const int cycles);
// executed exceptions
void A7TDMI_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data);
void A7TDMI_SupervisorCall(ARM* ARM, const ARM_Instr instr_data);
// copies for thumb
void T7TDMI_UndefinedInstruction(ARM* ARM, const ARM_Instr instr_data);
void T7TDMI_SupervisorCall(ARM* ARM, const ARM_Instr instr_data);

[[nodiscard]] ARM_PSR A7TDMI_GetSPSR(ARM7TDMI* a7tdmi);
void A7TDMI_SetSPSR(ARM7TDMI* a7tdmi, ARM_PSR psr);

// read register.
[[nodiscard]] u32 A7TDMI_GetReg(ARM7TDMI* a7tdmi, const int reg);
// write register.
void A7TDMI_SetReg(ARM7TDMI* a7tdmi, const int reg, u32 val);
// write program counter (r15).
void A7TDMI_SetPC(ARM7TDMI* a7tdmi, u32 val);

// add execute stage cycles, handle nonsequential code execution.
void A7TDMI_ExecuteCycles(ARM7TDMI* a7tdmi, const u32 Execute);

void A7TDMI_STR_Post(ARM7TDMI* a7tdmi);
void A7TDMI_LDR_Post(ARM7TDMI* a7tdmi);
void A7TDMI_STM_Post(ARM7TDMI* a7tdmi);
void A7TDMI_LDM_Post(ARM7TDMI* a7tdmi);
void A7TDMI_SWPLoad_Post(ARM7TDMI* a7tdmi);
void A7TDMI_SWPStore_Post(ARM7TDMI* a7tdmi);

void A7TDMI_DataRead(ARM7TDMI* a7tdmi, const timestamp now);
void A7TDMI_InstrRead(ARM7TDMI* a7tdmi, const timestamp now);
void A7TDMI_DataWrite(ARM7TDMI* a7tdmi, const timestamp now);

void A7TDMI_InstrReadPost(ARM7TDMI* a7tdmi, const timestamp now, u32 rdata);
void A7TDMI_DataPost(ARM7TDMI* a7tdmi, const timestamp now, u32 rdata);

void A7TDMI_RotateExtendUnit(u32* rdata, const u32 addr, const ARM_DataWidth size, const bool signext);

// temp
void A7TDMI_Log(ARM7TDMI* a7tdmi);
