#include <stdlib.h>
#include <stdio.h>
#include "../core/console.h"
#include "../core/arm/arm9/instr_luts.h"
#include "../core/arm/arm9/arm.h"


int main()
{
    LogMask = u64_max; // temp

    // init arm luts
    ARM9_InitInstrLUT();
    THUMB9_InitInstrLUT();

    FILE* ntr9 = fopen("ntr9.bin", "rb");

    if (ntr9 == NULL)
    {
        printf("no ntr arm9 bios :(\n");
        return EXIT_FAILURE;
    }

    // initialize main emulator state struct
    struct Console* sys = Console_Init(nullptr, ntr9);
    if (sys == nullptr)
    {
        return EXIT_FAILURE;
    }
    //ARM9_Log(&sys->ARM9);

    Console_MainLoop(sys);

    // TEMP: debugging
    /*for (int i = 0; i < 2; i++)
    {
        if (i == 0)
        {
            sys->ARM9.ARM.CPSR.Flags = 0;
            sys->ARM9.ARM.CPSR.QSticky = 0;
        }
        else
        {
            sys->ARM9.ARM.CPSR.Flags = 0xF;
            sys->ARM9.ARM.CPSR.QSticky = 1;
        }
        sys->ARM9.ARM.R[5] = 0x80000000;
        sys->ARM9.ARM.R[6] = 65;
        //sys->ARM9.ARM.CPSR.Carry = 1;
        sys->ARM9.ARM.Instr[1].Raw = 0xE1B02655;

        Console_MainLoop(sys);
    }*/

    return EXIT_SUCCESS;
}
