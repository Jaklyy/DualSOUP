#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "console.h"
#include "arm/arm9/arm.h"
#include "dma/dma.h"
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
    sys->HandleMain = CR_Active();

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

void Console_DirectBoot(struct Console* sys, FILE* rom)
{

    sys->IO.WRAMCR = 3;

    fseek(rom, 0x20, SEEK_SET);
    u32 vars[8];

    fread(vars, 4*4*2, 1, rom);

    fseek(rom, vars[0], SEEK_SET);
    {
        if (vars[3] > MiB(512)) return;
    timestamp nop;
    u32* arry = malloc(vars[3]);

    fseek(rom, vars[0], SEEK_SET);
    fread(arry, vars[3], 1, rom);
    bool nopy;

    for (unsigned i = 0; i < vars[3]/4; i++)
    {
        AHB9_Write(sys, &nop, vars[2], arry[i], 0xFFFFFFFF, false, false, &nopy);
        vars[2]+=4;
    }
    free(arry);
    }
    {
        if (vars[7] > MiB(512)) return;
    timestamp nop;
    u32* arry = malloc(vars[7]);

    fseek(rom, vars[4], SEEK_SET);
    fread(arry, vars[7], 1, rom);
    bool nopy;

    for (unsigned i = 0; i < vars[7]/4; i++)
    {
        AHB7_Write(sys, &nop, vars[6], arry[i], 0xFFFFFFFF, false, false, &nopy);
        vars[6]+=4;
    }
    free(arry);
    }

    memset(&sys->AHB7, 0, sizeof(sys->AHB7));
    memset(&sys->AHB9, 0, sizeof(sys->AHB9));
    memset(&sys->BusMR, 0, sizeof(sys->BusMR));


    ARM9_SetPC(&sys->ARM9, vars[1], 0);
    ARM7_SetPC(&sys->ARM7, vars[5]);
}

void Console_Reset(struct Console* sys)
{
    ARM9_Reset(&sys->ARM9, false /*unverified I guess?*/, true);
    ARM7_Reset(&sys->ARM7);

    sys->DMA9.ChannelTimestamps[0] = timestamp_max;
    sys->DMA9.ChannelTimestamps[1] = timestamp_max;
    sys->DMA9.ChannelTimestamps[2] = timestamp_max;
    sys->DMA9.ChannelTimestamps[3] = timestamp_max;
}

void Console_Scheduler(struct Console* sys)
{
    
}

void Console_MainLoop(struct Console* sys)
{
    while(true)
    {
        if ((sys->ARM9.ARM.Timestamp / 2) < sys->ARM7.ARM.Timestamp)
            CR_Switch(sys->HandleARM9);
        else
            CR_Switch(sys->HandleARM7);
    }
}
