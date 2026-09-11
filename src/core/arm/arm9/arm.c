#include "core/scheduler.h"
#include "core/utils.h"
#include "core/console.h"
#include "../shared/arm.h"
#include "instr_il.h"
#include "arm.h"




#define cpu (&a946->ARM)

// TEMP: debugging
void A946_Log(ARM946ES* a946[[maybe_unused]])
{
#if 0
    LogPrint(LOG_ARM9, "DUMPING ARM9 STATE:\n");
    for (int i = 0; i < 16; i++)
    {
        LogPrint(LOG_ARM9, "R%2i: %08X ", i, cpu->R[i]);
    }
    //LogPrint(LOG_ARM9, "R2:%08X\n", cpu->R[2]);
    LogPrint(LOG_ARM9, "CPSR:%08X\n", cpu->CPSR.Raw);
    LogPrint(LOG_ARM9, "INSTR: %08X ", cpu->Instr[0].Raw);
    LogPrint(LOG_ARM9, "DTCM: %08lX %08lX %i ITCM: %i %i %i\n", a946->CP15.DTCMReadBase, a946->CP15.DTCMWriteBase, a946->CP15.DTCMShift, a946->CP15.ITCMShift, a946->CP15.CR.ITCMEnable, a946->CP15.CR.ITCMLoadMode);
    LogPrint(LOG_ARM9, "EXE:%li MEM:%li\n\n", cpu->Timestamp, a946->MemTimestamp);
#endif
}

void A946_Init(ARM946ES* a946, Console* sys)
{
    ARM_Init(cpu, sys, ARM9ID);

    // set permanently set CP15 CR bits
    a946->CP15.CR.FixedOnes = 0xF;

    // set as no interlocks
    a946->RegIL.Raw = -1;

    // TODO: initial values are placeholders
    a946->CP15.DCachePRNG = 0x0123456789ABCDEF;
    a946->CP15.ICachePRNG = 0xFEDCBA9876543210;
}

#undef cpu
#define cpu (&a9es->ARM)

ARM_PSR A9ES_GetSPSR(ARM946ES* a9es)
{
    switch(cpu->CPSR.Mode)
    {
    case ARMMode_FIQ:
        return cpu->FIQ_Bank.SPSR;
    case ARMMode_IRQ:
        return cpu->IRQ_Bank.SPSR;
    case ARMMode_SVC:
        return cpu->SVC_Bank.SPSR;
    case ARMMode_SVC+1 ... ARMMode_ABT:
        return cpu->ABT_Bank.SPSR;
    case ARMMode_ABT+1 ... ARMMode_UND:
        return cpu->UND_Bank.SPSR;
    case ARMMode_USR:
    case ARMMode_UND+1 ... ARMMode_SYS:
        return cpu->CPSR;
    default: unreachable();
    }
}

void A9ES_SetSPSR(ARM946ES* a9es, ARM_PSR psr)
{
    switch(cpu->CPSR.Mode)
    {
    case ARMMode_FIQ:                   cpu->FIQ_Bank.SPSR = psr; break;
    case ARMMode_IRQ:                   cpu->IRQ_Bank.SPSR = psr; break;
    case ARMMode_SVC:                   cpu->SVC_Bank.SPSR = psr; break;
    case ARMMode_SVC+1 ... ARMMode_ABT: cpu->ABT_Bank.SPSR = psr; break;
    case ARMMode_ABT+1 ... ARMMode_UND: cpu->UND_Bank.SPSR = psr; break;
    case ARMMode_USR:
    case ARMMode_UND+1 ... ARMMode_SYS: break; // no spsr, no write
    default: unreachable();
    }
    return;
}

u32 A9ES_GetReg(ARM946ES* a9es, const u8 reg)
{
    // todo: strd/ldrd incorrect forwarding errata
    return cpu->R[reg];
}

void A9ES_SetPC(ARM946ES* a9es, u32 addr)
{
    // arm9 enforces pc alignment properly in arm mode.
    addr &= ~(cpu->CPSR.Thumb ? 0x1 : 0x3);

    cpu->PC = addr;
    cpu->FlushProg = 3;
}

void A9ES_SetReg(ARM946ES* a9es, const u8 reg, u32 val)
{
    // PC must be handled specially
    if (reg == 15) A9ES_SetPC(a9es, val);
    else cpu->R[reg] = val; // I pray that nothing makes it any more complex than this.
}

void A9ES_ExecuteCycles(ARM946ES* a9es, const u8 execute)
{
    //if (execute) a9es->RegIL.Raw >>= 8; // step interlock tracker // actually i dont think i need this...?
    cpu->Timestamp += DSClk67(execute);
}

void A9ES_DataGo(ARM946ES* a9es, const A9ES_PostMem* postmem)
{
    a9es->PostMem = *postmem;
    a9es->BusFlags.DataGo = true;
    a9es->BusFlags.DataBusy = false;
    a9es->BusFlags.DataDone = false;
}
void A9ES_DataBusy(ARM946ES* a9es)
{
    a9es->BusFlags.DataGo = false;
    a9es->BusFlags.DataBusy = true;
    a9es->BusFlags.DataDone = false;
}
void A9ES_DataDone(ARM946ES* a9es)
{
    a9es->BusFlags.DataGo = false;
    a9es->BusFlags.DataBusy = false;
    a9es->BusFlags.DataDone = true;
}
void A9ES_DataNone(ARM946ES* a9es)
{
    a9es->BusFlags.DataGo = false;
    a9es->BusFlags.DataBusy = false;
    a9es->BusFlags.DataDone = false;
}

void A9ES_InstrGo(ARM946ES* a9es, const bool late)
{
    a9es->BusFlags.InstrGo = !late;
    a9es->BusFlags.InstrBusy = false;
    a9es->BusFlags.InstrDone = false;
    a9es->BusFlags.InstrLate = late;
}
void A9ES_InstrBusy(ARM946ES* a9es)
{
    a9es->BusFlags.InstrGo = false;
    a9es->BusFlags.InstrBusy = true;
    a9es->BusFlags.InstrDone = false;
}
void A9ES_InstrDone(ARM946ES* a9es)
{
    a9es->BusFlags.InstrGo = false;
    a9es->BusFlags.InstrBusy = false;
    a9es->BusFlags.InstrDone = true;
    a9es->BusFlags.InstrLate = false;
}
void A9ES_InstrNone(ARM946ES* a9es)
{
    a9es->BusFlags.InstrGo = false;
    a9es->BusFlags.InstrBusy = false;
    a9es->BusFlags.InstrDone = false;
    a9es->BusFlags.InstrLate = false;
}

[[nodiscard]] bool A9ES_CheckInterrupts(ARM946ES* a9es)
{
    if (!cpu->CPSR.IRQDisable && cpu->InterruptRequest)
    {
        A9ES_InterruptRequest(a9es);
        return true;
    }
    else return false;
}

/*  order of operations:
    1. pipeline is stepped
    2. interlocks are stalled for
    3. code is fetched
    4. irqs are checked
    5. instruction is executed
*/

// interlocks are stalled for before fetching
// note: this is probably be checked for on the prior decode stage, but i dont think that matters?
s8 A9ES_DecodeInterlocks(ARM946ES* a9es, const bool thumb, const s8 reg, const s8 len, const s8 len_c, bool* retry)
{
    if (thumb)
    {
        const ARM_Instr instr = cpu->Instr[1];
        const u16 decode = (instr.Thumb >> 10);
        return T9ES_InterlockLUT[decode](instr, reg, len, len_c, retry);
    }
    else
    {
        const ARM_Instr instr = cpu->Instr[1];
        const u8 condcode = instr.Arm >> 28;
        const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

        // decode instructions
        if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
            return A9ES_InterlockLUT[decode](instr, reg, len, len_c, retry);
        else if (condcode == ARMCond_NV)
            return A9ES_Uncond_Interlocks(instr, reg, len, len_c, retry);
        else // checkme: i dont think failed condcode instructions stall for interlocks.
            return 0;
    }
}

void A9ES_SetTwoCycleInterlock(ARM946ES* a9es, const u8 reg)
{
    a9es->RegIL.Next = reg | 0x80;
}

s8 A9ES_TestTwoCycleInterlocks(ARM946ES* a9es)
{
    u8 cur = a9es->RegIL.Cur;
    a9es->RegIL.Raw >>= 8; // step interlock tracker

    if (cur & 0x10) return 0; // reg is NOT interlocked
    else return A9ES_DecodeInterlocks(a9es, cpu->CPSR.Thumb, cur & 0xF, 1, 1, nullptr);
}

void A9ES_Exec(ARM946ES* a9es)
{
    if (!A9ES_CheckInterrupts(a9es))
    {
        if (cpu->CPSR.Thumb)
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u16 decode = (instr.Thumb >> 10);

            T9ES_InstructionLUT[decode](cpu, instr);
        }
        else
        {
            const ARM_Instr instr = cpu->Instr[0];
            const u8 condcode = instr.Arm >> 28;
            const u16 decode = ((instr.Arm >> 16) & 0xFF0) | ((instr.Arm >> 4) & 0xF);

            // first we need to check the condition code (should be part of decoding?)
            if (ARM_ConditionLookup(condcode, cpu->CPSR.Flags))
            {
                A9ES_InstructionLUT[decode](cpu, instr);
            }
            else if (condcode == ARMCond_NV) // unconditional instructions
            {
                A9ES_Uncond(cpu, instr);
            }
            else if (decode == 0x127) // BKPT is decoded weird and seemingly overrides condition code handling (always passes)
            {
                A9ES_PrefetchAbort(cpu, instr);
            }
            else // actually an instruction that failed the condition check.
            {
                A9ES_ExecuteCycles(a9es, 0);
                ARM_StepPC(cpu, false);
            }
        }
    }
}
#undef cpu
#define cpu (&a946->ARM)

void A946_Run(ARM946ES* a946, timestamp now)
{
    cpu->Timestamp = now; // hacky: TODO: rework this
    if (a946->BusFlags.DataGo || a946->BusFlags.InstrGo)
    {
        do
        {
            if (a946->BusFlags.InstrGo)
                A946_InstrRead(a946, now);
            else
            {
                if (a946->PostMem.DataCB == A9ESDataCB_LoadSingle || a946->PostMem.DataCB == A9ESDataCB_LoadMultiple)
                    A946_DataRead(a946, now);
                else
                    A946_DataWrite(a946, now);

                if (a946->BusFlags.InstrLate && (a946->PostMem.NumFetchCompleted == a946->PostMem.NumFetch))
                    a946->BusFlags.InstrGo = true;
            }
        }
        while (a946->BusFlags.DataGo || a946->BusFlags.InstrGo);
    }

    if (a946->BusFlags.InstrBusy || a946->BusFlags.DataBusy) return; // don't reschedule
    else if (a946->BusFlags.InstrDone || a946->BusFlags.DataDone)
    {
        timestamp next = cpu->Timestamp;
        if (a946->BusFlags.InstrDone)
        {
            DS_CLAMP(next, <, a946->InstrTS)
            if (cpu->FlushProg > 0)
            {
                cpu->FlushProg--;
                ARM_PipelineStep(cpu);
                ARM_StepPC(cpu, cpu->CPSR.Thumb);
                A9ES_InstrGo(a946, false);
            }
            else A9ES_InstrNone(a946);
        }

        if (a946->BusFlags.DataDone)
        {
            DS_CLAMP(next, <, a946->DataTS)
            cpu->Timestamp = next;
            A9ES_MemCallbacks(a946);
        }
        else cpu->Timestamp = next;
    }
    else if (!cpu->WaitForInterrupt)
    {
        A9ES_Exec(a946);// execute next instruction if no accesses are waiting
        if (!a946->BusFlags.DataGo && !a946->BusFlags.InstrGo) // hacky code fetch scheduling
        {
            cpu->Timestamp += DSClk67(A9ES_TestTwoCycleInterlocks(a946)); // add interlocks
            A9ES_InstrGo(a946, false);
        }
        ARM_PipelineStep(cpu);
    }

    if (!cpu->WaitForInterrupt)
        Sched_AddEvent(cpu->Sys, cpu->Timestamp, Evt_ARM9);
}
#undef cpu
