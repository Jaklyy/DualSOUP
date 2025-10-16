#include <string.h>
#include <stdlib.h>
#include "console.h"
#include "utils.h"




bool Console_Init(struct Console* sys)
{
    if (sys == nullptr)
    {
        // allocate and initialize 
        sys = aligned_alloc(alignof(struct Console), sizeof(struct Console));

        if (sys == NULL)
        {
            LogPrint(LOG_ALWAYS, "FATAL: Memory allocation failed.\n");
            return false;
        }
    }
    else
    {
        // de-allocate coroutine handles
        CR_Free(sys->HandleARM9);
        CR_Free(sys->HandleARM7);
    }

    // wipe entire emulator state
    memset(sys, 0, sizeof(*sys));
    ARM9_Init(&sys->ARM9, sys);
    ARM7_Init(&sys->ARM7, sys);

    // initialize coroutine handles
    sys->HandleARM9 = CR_Create((void*)ARM9_MainLoop, &sys->ARM9);
    if (sys->HandleARM9 == cr_null)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle allocation failed.\n");
        return false;
    }

    Console_Reset(sys);

    return true;
}

void Console_Reset(struct Console* sys)
{
    ARM9_Reset(&sys->ARM9, false /*unverified I guess?*/, true);
    ARM7_Reset(&sys->ARM7);
}

void Console_Scheduler(struct Console* sys)
{
    
}

void Console_MainLoop(struct Console* sys)
{
    //ARM9_Step(&sys->ARM9);
    CR_Switch(sys->HandleARM9);
}
