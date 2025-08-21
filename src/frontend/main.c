#include <stdlib.h>
#include "../core/console.h"
#include "../core/arm/arm9/instr_luts.h"
#include "../core/arm/arm9/arm.h"


int main()
{
    struct Console* sys = aligned_alloc(alignof(struct Console), sizeof(struct Console));
    LogMask = u64_max; // temp

    // init arm luts
    ARM9_InitInstrLUT();
    THUMB9_InitInstrLUT();

    Console_Init(sys);

    // TEMP: debugging
    sys->ARM9.ARM.R[1] = 0x80000000;
    sys->ARM9.ARM.R[0] = 0xFFFFFFFF;
    sys->ARM9.ARM.Instr[1] = 0xE0910000;

    Console_MainLoop(sys);

    ARM9_Log(&sys->ARM9);

    return EXIT_SUCCESS;
}
