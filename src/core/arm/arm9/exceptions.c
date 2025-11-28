#include "arm.h"




#define cpu ((struct ARM*)ARM9)

[[nodiscard]] u32 ARM9_GetExceptionBase(struct ARM946ES* ARM9)
{
    return (ARM9->CP15.CR.HiVector ? 0xFFFF0000 : 0x00000000);
}

void ARM9_Reset(struct ARM946ES* ARM9, const bool itcm, const bool hivec)
{
    // TODO: how many cycles does this take?
    // 

    // TODO: does cache prng ever get reset? (does it ever get explicitly initialized?)

    // we probably want to wait for the cache stream to end...?
    // it might be interrupted immediately though...?
    bool ARM9_ProgressCacheStream(timestamp* ts, struct ARM9_CacheStream* stream, u32* ret, const bool seq);
    ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->IStream, nullptr, false);
    ARM9_ProgressCacheStream(&ARM9->ARM.Timestamp, &ARM9->DStream, nullptr, false);

    // SPECULATIVE: arm docs explicitly state that R14_SVC and SPSR_SVC have an "unpredictable value" when reset is de-asserted
    // which could mean literally anything
    // it is entirely possible that the old pc and cpsr are banked by the processor
    // or at least it tries to and instead puts some nonsense in them?
    // ...or it could just mean that they aren't reset in any way......
    // im gonna bank em for funsies.
    // Note: this is apparently actually what the ARM7TDMI does, according to documentation.
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_SWI);

    // one can only imagine what pc would be here... probably depends on when the current instruction got interrupted?
    u32 oldpc = cpu->PC;
    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    // set cpsr bits
    // flag bits dont seem to be mentioned anywhere?
    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    // reset control reg
    ARM9->CP15.CR.ITCMLoadMode = false;
    ARM9->CP15.CR.DTCMLoadMode = false;
    ARM9->CP15.CR.DTCMEnable = false;
    ARM9->CP15.CR.NoLoadTBit = false;
    ARM9->CP15.CR.CacheRR = false;
    ARM9->CP15.CR.ICacheEnable = false;
    ARM9->CP15.CR.BigEndian = false;
    ARM9->CP15.CR.DCacheEnable = false;
    ARM9->CP15.CR.MPUEnable = false;

    // these two are configurable via input pins.
    // exception vector is obviously default set since that's where our bootcode is.
    // itcm enable is less clear, it's probably not important since all relevant bootroms
    // should explicitly set this before using it, but it'd be nice to know ig.
    // Resetting with ITCM on + Low vectors is apparently an intended usecase?
    // for some reason they mention that if you're initializing tcms you should
    // use the drain write buffer instruction before asserting reset...?
    // but write buffer doesn't work with tcms.....?
    ARM9->CP15.CR.ITCMEnable = itcm;
    ARM9->CP15.CR.HiVector = hivec;

    // only the enable bit is cleared.
    for (int i = 0; i < 8; i++)
    {
        ARM9->CP15.MPURegionCR[i].Enable = false;
    }

    // bases are reset to 0.
    // sizes are reset to physical sizes.
    // god only knows why they do this for dtcm?
    ARM9->CP15.DTCMCR.Size = ARM9_CP15DTCMSize;
    ARM9->CP15.DTCMCR.BaseAddr = 0;
    ARM9->CP15.ITCMCR.Size = ARM9_CP15ITCMSize;
    // ITCM region base is also mentioned as being explicitly reset... for.... reasons...? i guess...?
    // CHECKME: Does setting it do anything?????
    ARM9->CP15.ITCMCR.BaseAddr = 0;

    ARM9->CP15.TraceProcCR = 0;

    // both caches are invalidated (valid flag cleared)
    for (unsigned i = 0; i < ARM9_ITagNum; i++)
    {
        ARM9->ITagRAM[i].Valid = false;
    }
    for (unsigned i = 0; i < ARM9_DTagNum; i++)
    {
        ARM9->DTagRAM[i].Valid = false;
    }

    // TODO: MURDER WRITE BUFFER CONTENTS HERE

    // reset state unspecified.
    // ARM946E-S manual says "all cp15 reg bits that're both defined and contain state are reset to 0 on reset assertion unless stated otherwise"
    // So I guess we can pretend to trust documentation for now?
    ARM9->CP15.DCacheConfig = 0;
    ARM9->CP15.ICacheConfig = 0;
    ARM9->CP15.WriteBufferConfig = 0;
    ARM9->CP15.DCacheLockdownCR.Raw = 0;
    ARM9->CP15.ICacheLockdownCR.Raw = 0;
    ARM9->CP15.TraceProcIdReg = 0; // i guess this should be reset too?
    // TODO: BIST stuff is probably reset too.

    // NOTE: Data and instr perms are states to be "undefined"

    // reset all this junk.
    ARM9_ConfigureITCM(ARM9);
    ARM9_ConfigureDTCM(ARM9);
    for (int i = 0; i < 8; i++)
        ARM9_ConfigureMPURegionSize(ARM9, i);

    ARM9_ConfigureMPURegionPerms(ARM9);

    // cheat to encode mpu off perms in rgn 7
    ARM9->CP15.MPURegionPermsPriv[7].Read = true;
    ARM9->CP15.MPURegionPermsPriv[7].Write = true;
    ARM9->CP15.MPURegionPermsPriv[7].Exec = true;
    ARM9->CP15.MPURegionPermsPriv[7].DCache = false;
    ARM9->CP15.MPURegionPermsPriv[7].ICache = false;
    ARM9->CP15.MPURegionPermsPriv[7].Buffer = false;
    ARM9->CP15.MPURegionPermsUser[7].Read = true;
    ARM9->CP15.MPURegionPermsUser[7].Write = true;
    ARM9->CP15.MPURegionPermsUser[7].Exec = true;
    ARM9->CP15.MPURegionPermsUser[7].DCache = false;
    ARM9->CP15.MPURegionPermsUser[7].ICache = false;
    ARM9->CP15.MPURegionPermsUser[7].Buffer = false;
    ARM9->CP15.MPURegionBase[7] = 0;
    ARM9->CP15.MPURegionMask[7] = 0;

    cpu->CpuSleeping = 0;

    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_RST, 0);
}

void ARM9_RaiseUDF(struct ARM* ARM, const struct ARM_Instr instr_data, const int execycles, const int memcycles)
{
    struct ARM946ES* ARM9 = (struct ARM946ES*)ARM;
    if (cpu->CPSR.Thumb)
        LogPrint(LOG_ARM9 | LOG_EXCEP, "THUMB9 - UNDEF INSTR: %04X @ %08X\n", instr_data.Raw, cpu->PC);
    else
        LogPrint(LOG_ARM9 | LOG_EXCEP, "ARM9 - UNDEF INSTR: %08X @ %08X\n", instr_data.Raw, cpu->PC);


    CrashSpectacularly("FARK %08lX\n", cpu->PC);

    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM9_ExecuteCycles(ARM9, execycles, memcycles);

    ARM_SetMode(cpu, ARMMode_UND);

    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_UND, 0);
}

void ARM9_UndefinedInstruction(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM9_RaiseUDF(ARM, instr_data, 1, 1);
}

void THUMB9_UndefinedInstruction(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM9_RaiseUDF(ARM, instr_data, 1, 1);
}

void ARM9_SoftwareInterrupt(struct ARM* ARM, [[maybe_unused]] const struct ARM_Instr instr_data)
{
    // TODO: could add a print here for logging software interrupts that gets fired.
    struct ARM946ES* ARM9 = (struct ARM946ES*)ARM;

    // addr of next instr
    u32 oldpc = cpu->PC - (cpu->CPSR.Thumb ? 2 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM9_ExecuteCycles(ARM9, 1, 1);

    ARM_SetMode(cpu, ARMMode_SWI);

    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_SWI, 0);
}

void THUMB9_SoftwareInterrupt(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM9_SoftwareInterrupt(ARM, instr_data);
}

void ARM9_PrefetchAbort(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    struct ARM946ES* ARM9 = (struct ARM946ES*)ARM;

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
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM9_ExecuteCycles(ARM9, 1, 1);

    ARM_SetMode(cpu, ARMMode_ABT);

    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_PAB, 0);
}

void THUMB9_PrefetchAbort(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    ARM9_PrefetchAbort(ARM, instr_data);
}

// TODO: this should probably take an address as input for testing purposes?
void ARM9_DataAbort(struct ARM946ES* ARM9)
{
    LogPrint(LOG_ARM9 | LOG_EXCEP, "%s9 - DATA ABT @ %08X\n", (cpu->CPSR.Thumb ? "THUMB" : "ARM"), cpu->PC);

    // lr is aborted instr + 8
    // CHECKME: what happens if the abort was from an exception return LDM? (SPSR was restored?)
    u32 oldpc = cpu->PC + ((cpu->CPSR.Thumb) ? 4 : 0);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM_SetMode(cpu, ARMMode_ABT);

    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_DAB, 0);
}

void ARM9_InterruptRequest(struct ARM946ES* ARM9)
{
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM9_ExecuteCycles(ARM9, 1, 1);

    ARM_SetMode(cpu, ARMMode_IRQ);

    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;

    cpu->CpuSleeping = 0;

    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_IRQ, 0);
}

void ARM9_FastInterruptRequest(struct ARM946ES* ARM9)
{
    // lr is next instr + 4
    u32 oldpc = cpu->PC - ((cpu->CPSR.Thumb) ? 0 : 4);
    union ARM_PSR oldcpsr = cpu->CPSR;

    ARM9_ExecuteCycles(ARM9, 1, 1);

    ARM_SetMode(cpu, ARMMode_FIQ);

    cpu->LR = oldpc;
    ARM9_SetSPSR(ARM9, oldcpsr);

    cpu->CPSR.Thumb = false;
    cpu->CPSR.IRQDisable = true;
    cpu->CPSR.FIQDisable = true;

    cpu->CpuSleeping = 0;

    ARM9_SetPC(ARM9, ARM9_GetExceptionBase(ARM9) + ARMVector_FIQ, 0);
}

#undef cpu
