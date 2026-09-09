#include "arm.h"




[[nodiscard]] u32 A946_GetExceptionBase(ARM946ES* a946)
{
    return (a946->CP15.CR.HiVector ? 0xFFFF0000 : 0x00000000);
}

void A946_Reset(ARM946ES* a946, const bool itcm, const bool hivec)
{
    ARM* cpu = &a946->ARM;
    // TODO: how many cycles does this take?

    // TODO: does cache prng ever get reset? (does it ever get explicitly initialized?)

    // we probably want to wait for the cache stream to end...?
    // it might be interrupted immediately though...?

    // SPECULATIVE: arm docs explicitly state that R14_SVC and SPSR_SVC have an "unpredictable value" when reset is de-asserted
    // which could mean literally anything
    // it is entirely possible that the old pc and cpsr are banked by the processor
    // or at least it tries to and instead puts some nonsense in them?
    // ...or it could just mean that they aren't reset in any way......
    // im gonna bank em for funsies.
    // Note: this is apparently actually what the ARM7TDMI does, according to documentation.
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_SVC);

    // one can only imagine what pc would be here... probably depends on when the current instruction got interrupted?
    u32 oldpc = cpu->PC;
    cpu->LR = oldpc;
    A9ES_SetSPSR(a946, oldcpsr);

    // set cpsr bits
    // flag bits dont seem to be mentioned anywhere?
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    // reset control reg
    a946->CP15.CR.ITCMLoadMode = false;
    a946->CP15.CR.DTCMLoadMode = false;
    a946->CP15.CR.DTCMEnable = false;
    a946->CP15.CR.NoLoadTBit = false;
    a946->CP15.CR.CacheRR = false;
    a946->CP15.CR.ICacheEnable = false;
    a946->CP15.CR.BigEndian = false;
    a946->CP15.CR.DCacheEnable = false;
    a946->CP15.CR.MPUEnable = false;

    // these two are configurable via input pins.
    // exception vector is obviously default set since that's where our bootcode is.
    // itcm enable is less clear, it's probably not important since all relevant bootroms
    // should explicitly set this before using it, but it'd be nice to know ig.
    // Resetting with ITCM on + Low vectors is apparently an intended usecase?
    // for some reason they mention that if you're initializing tcms you should
    // use the drain write buffer instruction before asserting reset...?
    // but write buffer doesn't work with tcms.....?
    a946->CP15.CR.ITCMEnable = itcm;
    a946->CP15.CR.HiVector = hivec;

    // only the enable bit is cleared.
    for (int i = 0; i < 8; i++)
    {
        a946->CP15.MPURegionCR[i].Enable = false;
    }

    // bases are reset to 0.
    // sizes are reset to physical sizes.
    // god only knows why they do this for dtcm?
    a946->CP15.DTCMCR.Size = A946_CP15DTCMSize;
    a946->CP15.DTCMCR.BaseAddr = 0;
    a946->CP15.ITCMCR.Size = A946_CP15ITCMSize;
    // ITCM region base is also mentioned as being explicitly reset... for.... reasons...? i guess...?
    // CHECKME: Does setting it do anything?????
    a946->CP15.ITCMCR.BaseAddr = 0;

    a946->CP15.TraceProcCR = 0;

    // both caches are invalidated (valid flag cleared)
    for (unsigned i = 0; i < A946_ITagNum; i++)
    {
        a946->ITagRAM[i].Valid = false;
    }
    for (unsigned i = 0; i < A946_DTagNum; i++)
    {
        a946->DTagRAM[i].Valid = false;
    }

    a946->BIU.WBuffer.Empty = true;
    a946->BIU.WBuffer.FIFODrainPtr = 0;
    a946->BIU.WBuffer.FIFOFillPtr = 0;

    // reset state unspecified.
    // ARM946E-S manual says "all cp15 reg bits that're both defined and contain state are reset to 0 on reset assertion unless stated otherwise"
    // So I guess we can pretend to trust documentation for now?
    a946->CP15.DCacheConfig = 0;
    a946->CP15.ICacheConfig = 0;
    a946->CP15.WriteBufferConfig = 0;
    a946->CP15.DCacheLockdownCR.Raw = 0;
    a946->CP15.ICacheLockdownCR.Raw = 0;
    a946->CP15.TraceProcIdReg = 0; // i guess this should be reset too?
    // TODO: BIST stuff is probably reset too.

    // NOTE: Data and instr perms are stated to be "undefined"

    // reset all this junk.
    A946_ConfigureITCM(a946);
    A946_ConfigureDTCM(a946);
    for (int i = 0; i < 8; i++)
        A946_ConfigureMPURegionSize(a946, i);

    A946_ConfigureMPURegionPerms(a946);

    // cheat to encode mpu off perms in rgn 7
    a946->CP15.MPURegionPermsPriv[7].Read = true;
    a946->CP15.MPURegionPermsPriv[7].Write = true;
    a946->CP15.MPURegionPermsPriv[7].Exec = true;
    a946->CP15.MPURegionPermsPriv[7].DCache = false;
    a946->CP15.MPURegionPermsPriv[7].ICache = false;
    a946->CP15.MPURegionPermsPriv[7].Buffer = false;
    a946->CP15.MPURegionPermsUser[7].Read = true;
    a946->CP15.MPURegionPermsUser[7].Write = true;
    a946->CP15.MPURegionPermsUser[7].Exec = true;
    a946->CP15.MPURegionPermsUser[7].DCache = false;
    a946->CP15.MPURegionPermsUser[7].ICache = false;
    a946->CP15.MPURegionPermsUser[7].Buffer = false;
    a946->CP15.MPURegionBase[7] = 0;
    a946->CP15.MPURegionMask[7] = 0;

    cpu->CpuSleeping = 0;

    A9ES_SetPC(a946, A946_GetExceptionBase(a946) + ARMVector_RST);
}

void A9ES_RaiseUDF(ARM* cpu, const ARM_Instr instr_data, const s32 execycles)
{
    ARM946ES* a9es = (ARM946ES*)cpu;

    if (cpu->CPSR.Thumb)
        LogPrint(LOG_ARM9 | LOG_EXCEP, "THUMB9 - UNDEF INSTR: %04X @ %08X\n", instr_data.Raw, cpu->PC);
    else
        LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9 - UNDEF INSTR: %08X @ %08X\n", instr_data.Raw, cpu->PC);


    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    A9ES_ExecuteCycles(a9es, execycles-1);

    ARM_SetMode(cpu, ARMMode_UND);

    cpu->LR = oldpc;
    A9ES_SetSPSR(a9es, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    A9ES_SetPC(a9es, A946_GetExceptionBase(a9es) + ARMVector_UND);
}

void ARM9_UndefinedInstruction(ARM* cpu, const ARM_Instr instr_data)
{
    A9ES_RaiseUDF(cpu, instr_data, 1);
}

void THUMB9_UndefinedInstruction(ARM* cpu, const ARM_Instr instr_data)
{
    A9ES_RaiseUDF(cpu, instr_data, 1);
}

void A9ES_SupervisorCall(ARM* cpu, [[maybe_unused]] const ARM_Instr instr_data) // aka: software interrupt
{
    ARM946ES* a9es = (ARM946ES*)cpu;

    // TODO: could add a print here for logging software interrupts that gets fired.

    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_SVC);

    cpu->LR = oldpc;
    A9ES_SetSPSR(a9es, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    A9ES_SetPC(a9es, A946_GetExceptionBase(a9es) + ARMVector_SVC);
}

void T9ES_SupervisorCall(ARM* ARM, const ARM_Instr instr_data) // aka: software interrupt
{
    A9ES_SupervisorCall(ARM, instr_data);
}

void A9ES_PrefetchAbort(ARM* cpu, const ARM_Instr instr_data)
{
    ARM946ES* a9es = (ARM946ES*)cpu;

    if (instr_data.Aborted)
        LogPrint(LOG_ARM9 | LOG_EXCEP, "%s9 - PREFETCH ABT @ %08X\n", (cpu->CPSR.Thumb ? "THUMB" : "ARM"), cpu->PC);
    else
    {
        if (cpu->CPSR.Thumb)
            LogPrint(LOG_ARM9 | LOG_EXCEP, "THUMB9 - BKPT: %04X @ %08X\n", instr_data.Raw, cpu->PC);
        else
            LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9 - BKPT: %08X @ %08X\n", instr_data.Raw, cpu->PC);
    }

    // lr is aborted instruction + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_ABT);

    cpu->LR = oldpc;
    A9ES_SetSPSR(a9es, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    A9ES_SetPC(a9es, A946_GetExceptionBase(a9es) + ARMVector_PAB);
}

void T9ES_PrefetchAbort(ARM* ARM, const ARM_Instr instr_data)
{
    A9ES_PrefetchAbort(ARM, instr_data);
}

// TODO: this should probably take an address as input for testing purposes?
void A9ES_DataAbort(ARM946ES* a9es)
{
    ARM* cpu = &a9es->ARM;

    LogPrint(LOG_ARM9 | LOG_EXCEP, "%s9 - DATA ABT @ %08X\n", (cpu->CPSR.Thumb ? "THUMB" : "ARM"), cpu->PC);

    // lr is aborted instr + 8
    // CHECKME: what happens if the abort was from an exception return LDM? (SPSR was restored?)
    u32 oldpc = cpu->PC + ((cpu->CPSR.Thumb) ? 2 : -4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_ABT);

    cpu->LR = oldpc;
    A9ES_SetSPSR(a9es, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    A9ES_SetPC(a9es, A946_GetExceptionBase(a9es) + ARMVector_DAB);
}

void A9ES_InterruptRequest(ARM946ES* a9es)
{
    ARM* cpu = &a9es->ARM;

    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_IRQ);

    cpu->LR = oldpc;
    A9ES_SetSPSR(a9es, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;

    cpu->CpuSleeping = 0;

    A9ES_SetPC(a9es, A946_GetExceptionBase(a9es) + ARMVector_IRQ);
}

void ARM9_FastInterruptRequest(ARM946ES* a9es)
{
    ARM* cpu = &a9es->ARM;
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_FIQ);

    cpu->LR = oldpc;
    A9ES_SetSPSR(a9es, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->CpuSleeping = 0;

    A9ES_SetPC(a9es, A946_GetExceptionBase(a9es) + ARMVector_FIQ);
}

#undef cpu
