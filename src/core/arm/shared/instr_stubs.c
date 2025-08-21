
#include <stdlib.h>
#include "instr.h"




void ARM_UNIMPL(struct ARM* cpu, const u32 instr_data)
{
    LogPrint(1<<cpu->CPUID | LOG_UNIMP, "UNIMPLEMENTED ARM%i INSTR: %08X\n", (cpu->CPUID*2)+7, instr_data);
    exit(EXIT_FAILURE);
}

void THUMB_UNIMPL(struct ARM* cpu, const u16 instr_data)
{
    LogPrint(1<<cpu->CPUID | LOG_UNIMP, "UNIMPLEMENTED THUMB%i INSTR: %04X\n", (cpu->CPUID*2)+7, instr_data);
    exit(EXIT_FAILURE);
}
