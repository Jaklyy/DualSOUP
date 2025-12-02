#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include "../core/console.h"
#include "../core/arm/arm9/instr_luts.h"
#include "../core/arm/arm7/instr_luts.h"




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
        exit(EXIT_FAILURE);
    }

    FILE* ntr7 = fopen("ntr7.bin", "rb");
    if (ntr7 == NULL)
    {
        printf("no ntr arm7 bios :(\n");
        exit(EXIT_FAILURE);
    }

    FILE* ztst = fopen((char*)mailbox[1], "rb");
    if (ztst == NULL)
    {
        printf("no ztst :(\n");
        mailbox[3] = (void*)true;
        initflag = true;
        return EXIT_FAILURE;
    }

    // initialize main emulator state struct
    struct Console* sys = Console_Init((struct Console*)mailbox[2], ntr9, ntr7, (void*)mailbox[0]);
    if (sys == nullptr)
    {
        exit(EXIT_FAILURE);
    }

    mailbox[2] = sys;
    initflag = true;

    fclose(ntr9);
    fclose(ntr7);

    Console_DirectBoot(sys, ztst);
    Console_MainLoop(sys);

    sys->KillThread = false;

    return EXIT_SUCCESS;
}

int main()
{
    LogMask = u64_max; // temp

    // TODO investigate: SDL_HINT_TIMER_RESOLUTION
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        printf("SDLINIT ERROR!!!\n");
        return EXIT_FAILURE;
    }

    SDL_Window* win;
    SDL_Renderer* ren;

    if (!SDL_CreateWindowAndRenderer("DualSOUP", 256*2, 192*2*2, 0, &win, &ren))
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

    bool threadexists = false;
    thrd_t emu;
    struct Console* sys = nullptr;

    mtx_unlock(&init);

    SDL_Texture* blit = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, 256, 192*2);
    SDL_SetTextureScaleMode(blit, SDL_SCALEMODE_NEAREST); // TODO: SDL_SCALEMODE_PIXELART when 3.4.0 is out?
    SDL_Event evts;
    u8* buffer;
    while(true)
    {
        SDL_PollEvent(&evts);
        switch(evts.type)
        {
            case SDL_EVENT_QUIT:
                return EXIT_SUCCESS;
            case SDL_EVENT_DROP_FILE:
            {
                if (threadexists)
                {
                    sys->KillThread = true;
                    while(sys->KillThread);
                }

                printf("%s\n", ((SDL_DropEvent*)&evts)->data);
                initflag = false;
                mtx_init(&init, mtx_plain);
                mtx_lock(&init);
                volatile void* mailbox[4] = {pad, (volatile void*)((SDL_DropEvent*)&evts)->data, sys, 0};
                if (thrd_create(&emu, Core_Init, mailbox) != thrd_success)
                {
                    printf("thread init failure :(\n");
                    return EXIT_FAILURE;
                }

                while(initflag == false);

                if ((bool)mailbox[3])
                    break;

                sys = (void*)mailbox[2];

                threadexists = true;
                break;
            }
            default:
                break;
        }

        if (sys)
        {
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
        else
        {
            SDL_RenderClear(ren);
            SDL_RenderPresent(ren);
        }

    }
}
