#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include "../core/console.h"
#include "../core/arm/arm9/instr_luts.h"
#include "../core/arm/arm7/instr_luts.h"
#include "../core/arm/arm9/arm.h"




mtx_t init;
volatile bool initflag;

int Core_Init(void* pass)
{
    volatile void** mailbox = pass;
    // init arm luts
    ARM9_InitInstrLUT();
    THUMB9_InitInstrLUT();
    ARM7_InitInstrLUT();
    THUMB7_InitInstrLUT();

    FILE* ntr9 = fopen("ntr9.bin", "rb");
    if (ntr9 == NULL)
    {
        printf("no ntr arm9 bios :(\n");
        return EXIT_FAILURE;
    }

    FILE* ntr7 = fopen("ntr7.bin", "rb");
    if (ntr7 == NULL)
    {
        printf("no ntr arm7 bios :(\n");
        return EXIT_FAILURE;
    }

    // initialize main emulator state struct
    struct Console* sys = Console_Init(nullptr, ntr9, ntr7, mailbox[0]);
    if (sys == nullptr)
    {
        return EXIT_FAILURE;
    }

    mailbox[1] = sys;
    initflag = true;

    fclose(ntr9);
    fclose(ntr7);
    //ARM9_Log(&sys->ARM9);

    FILE* ztst = fopen("ztst.nds", "rb");
    if (ztst == NULL)
    {
        printf("no ztst :(\n");
        return EXIT_FAILURE;
    }

    Console_DirectBoot(sys, ztst);
    Console_MainLoop(sys);

    return 0;
}

int main()
{
    LogMask = 0;//u64_max; // temp

    // TODO investigate: SDL_HINT_TIMER_RESOLUTION
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        printf("SDLINIT ERROR!!!\n");
        return EXIT_FAILURE;
    }

    SDL_Window* win;
    SDL_Renderer* ren;

    if (!SDL_CreateWindowAndRenderer("DualSOUP", 256, 192*2, 0, &win, &ren))
    {
        printf("window/renderer init failure :(\n");
        return EXIT_FAILURE;
    }

    int num;
    SDL_JoystickID* joysticks = SDL_GetGamepads(&num);
    printf("joysticks: %i\n", num);

    SDL_Gamepad* pad = NULL;
    if (num)
    {
        pad = SDL_OpenGamepad(joysticks[0]);
    }

    thrd_t emu;
    volatile void* mailbox[2] = {pad, 0};

    initflag = false;
    mtx_init(&init, mtx_plain);
    mtx_lock(&init);
    if (thrd_create(&emu, Core_Init, mailbox) != thrd_success)
    {
        printf("thread init failure :(\n");
        return EXIT_FAILURE;
    }

    while(initflag == false);

    mtx_unlock(&init);
    struct Console* sys = (void*)mailbox[1];

    SDL_Texture* blit = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, 256, 192*2);
    SDL_Event evts;
    u8* buffer;
    while(true)
    {
        SDL_PollEvent(&evts);
        switch(evts.type)
        {
            case SDL_EVENT_QUIT:
                return EXIT_SUCCESS;
            default:
                break;
        }

        if (sys->Blitted)
        {
            mtx_lock(&sys->FrameBufferMutex);
            int pitch;
            SDL_LockTexture(blit, NULL, (void**)&buffer, &pitch);
            for (int s = 0; s < 2; s++)
                for (int y = 0; y < 192; y++)
                    for (int x = 0; x < pitch/4; x++)
                        for (int b = 0; b < 4; b++)
                        {
                            if (b == 4) continue;
                            buffer[(s*192*pitch)+(y*pitch)+(x*pitch/256)+b] = (((((sys->Framebuffer[s][y][x] >> (b*6)) & 0x3F) * 0xFF) / 0x3F));
                        }
            SDL_UnlockTexture(blit);
            mtx_unlock(&sys->FrameBufferMutex);
            SDL_RenderTexture(ren, blit, NULL, NULL);
            SDL_RenderPresent(ren);
            sys->Blitted = false;
        }

    }
}
