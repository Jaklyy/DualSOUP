#include "core/utils.h"
#include "instr.h"




void ARM_UNIMPL(ARM* cpu, const ARM_Instr instr_data)
{
    CrashSpectacularly("UNIMPLEMENTED ARM%i INSTR: %08"PRIX16" @ %08"PRIX32"\n", CPUIDtoCPUNum, instr_data.Raw, cpu->PC);
}

void THUMB_UNIMPL(ARM* cpu, const ARM_Instr instr_data)
{
    CrashSpectacularly("UNIMPLEMENTED THUMB%i INSTR: %04"PRIX16" @ %08"PRIX32"\n", CPUIDtoCPUNum, instr_data.Thumb, cpu->PC);
}
