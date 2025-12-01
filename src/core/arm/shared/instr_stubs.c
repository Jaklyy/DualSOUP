
#include <stdlib.h>
#include "instr.h"




void ARM_UNIMPL(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    LogPrint(LOG_CPUID | LOG_UNIMP, "UNIMPLEMENTED ARM%i INSTR: %08X @ %08X\n", CPUIDtoCPUNum, instr_data.Raw, cpu->PC);
    exit(EXIT_FAILURE);
}

void THUMB_UNIMPL(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    LogPrint(LOG_CPUID | LOG_UNIMP, "UNIMPLEMENTED THUMB%i INSTR: %04X @ %08X\n", CPUIDtoCPUNum, instr_data.Thumb, cpu->PC);
    exit(EXIT_FAILURE);
}
