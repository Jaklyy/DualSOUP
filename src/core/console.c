#include <string.h>
#include "console.h"




void Console_Init(struct Console* sys)
{
    memset(sys, 0, sizeof(*sys));
    ARM9_Init(&sys->ARM9, sys);
    ARM7_Init(&sys->ARM7, sys);
    Console_Reset(sys);
}

void Console_Reset(struct Console* sys)
{
    ARM9_Reset(&sys->ARM9, false /*unverified I guess?*/, true);
    ARM7_Reset(&sys->ARM7);
}

void Console_MainLoop(struct Console* sys)
{
    ARM9_Step(&sys->ARM9);
}

