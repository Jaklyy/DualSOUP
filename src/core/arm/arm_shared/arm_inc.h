#pragma once

#include "../arm7/arm.h"
#include "../arm9/arm.h"




#define GETREG(reg) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_GetReg((struct ARM7TDMI*)cpu, reg) \
    : ARM9_GetReg((struct ARM946ES*)cpu, reg) \
)

#define SETREG(reg, val, interlock, interlock_c) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_SetReg((struct ARM7TDMI*)cpu, reg, val) \
    : ARM9_SetReg((struct ARM946ES*)cpu, reg, val, interlock, interlock_c) \
)

#define EXECYCLES(exec7, exec9, mem9) \
((cpu->CPUID == ARM7ID) \
    ? ARM7_ExecuteCycles((struct ARM7TDMI*)cpu, exec7-1) \
    : ARM9_ExecuteCycles((struct ARM946ES*)cpu, exec9-1, mem9) \
)
