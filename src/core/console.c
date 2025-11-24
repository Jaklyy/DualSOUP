#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "console.h"
#include "utils.h"




// TODO: this function probably shouldn't manage memory on its own?
struct Console* Console_Init(struct Console* sys, FILE* ntr9, FILE* ntr7)
{
    if (sys == nullptr)
    {
        // allocate and initialize 
        sys = aligned_alloc(alignof(struct Console), sizeof(struct Console));

        if (sys == NULL)
        {
            LogPrint(LOG_ALWAYS, "FATAL: Memory allocation failed.\n");
            return nullptr;
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
        free(sys); // probably a good idea to not leak memory, just in case.
        return nullptr;
    }

    sys->HandleARM7 = CR_Create((void*)ARM7_MainLoop, &sys->ARM7);

    if (sys->HandleARM7 == cr_null)
    {
        LogPrint(LOG_ALWAYS, "FATAL: Coroutine handle allocation failed.\n");
        free(sys); // probably a good idea to not leak memory, just in case.
        return nullptr;
    }

    int num; 
    // allocate any internal or external ROMs
    if (ntr9 != NULL)
    {
        num = fread(sys->NTRBios9.b8, NTRBios9_Size, 1, ntr9);

        if (num != 1)
        {
            LogPrint(LOG_ALWAYS, "ERROR: ARM9 BIOS did not load properly. This will cause issues!!!\n");
            // TODO: this should probably return an error code to the frontend.
            exit(EXIT_FAILURE);
        }
    }

    if (ntr7 != NULL)
    {
        num = fread(sys->NTRBios7.b8, NTRBios7_Size, 1, ntr7);

        if (num != 1)
        {
            LogPrint(LOG_ALWAYS, "ERROR: ARM7 BIOS did not load properly. This will cause issues!!!\n");
            // TODO: this should probably return an error code to the frontend.
            exit(EXIT_FAILURE);
        }
    }

    Console_Reset(sys);

    return sys;
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
    CR_Switch(sys->HandleARM9);
}
