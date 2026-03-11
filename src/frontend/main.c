#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_audio.h>
#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include "../core/console.h"
#include "../core/arm/arm9/instr_luts.h"
#include "../core/arm/arm7/instr_luts.h"




enum InitFlag : u8
{
    Init_Busy = 0,
    Init_Success = 1,
    Init_Fail = 2,
};

typedef struct
{
    const char* rompath;
    struct Console* sys;
    SDL_Gamepad* pad;
    SDL_AudioStream* aud;
    u8 initflag;
} MailBox;

int Core_Init(void* pass)
{
    volatile MailBox* mailbox = pass;
    // init arm luts
    ARM9_InitInstrLUT();
    THUMB9_InitInstrLUT();
    ARM7_InitInstrLUT();
    THUMB7_InitInstrLUT();

    FILE* ntr9 = fopen("ntr9.bin", "rb");
    if (ntr9 == NULL)
    {
        printf("no ntr arm9 bios :(\n");
        mailbox->initflag = Init_Fail;
        exit(EXIT_FAILURE);
    }

    FILE* ntr7 = fopen("ntr7.bin", "rb");
    if (ntr7 == NULL)
    {
        printf("no ntr arm7 bios :(\n");
        fclose(ntr9);
        mailbox->initflag = Init_Fail;
        return EXIT_FAILURE;
    }

    FILE* firmware = fopen("firmware.bin", "rb");
    if (firmware == NULL)
    {
        printf("no firmware :(\n");
        fclose(ntr9);
        fclose(ntr7);
        mailbox->initflag = Init_Fail;
        return EXIT_FAILURE;
    }

    const char* rompath = mailbox->rompath;

    // initialize main emulator state struct
    struct Console* sys = Console_Init(mailbox->sys, ntr9, ntr7, firmware, rompath, mailbox->pad, mailbox->aud);
    if (sys == nullptr)
    {
        fclose(ntr9);
        fclose(ntr7);
        fclose(firmware);
        mailbox->initflag = Init_Fail;
        return EXIT_FAILURE;
    }

    mailbox->sys = sys;
    mailbox->initflag = Init_Success;

    fclose(ntr9);
    fclose(ntr7);
    fclose(firmware);

#ifdef USEDIRECTBOOT
    Console_DirectBoot(sys);
#endif
    Console_MainLoop(sys);

    sys->KillThread = false;

    return EXIT_SUCCESS;
}

int main()
{
    LogMask = u64_max; // temp

    // TODO investigate: SDL_HINT_TIMER_RESOLUTION
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_AUDIO))
    {
        printf("SDLINIT ERROR!!!\n");
        return EXIT_FAILURE;
    }

    SDL_Window* win;
    SDL_Renderer* ren;

    if (!SDL_CreateWindowAndRenderer("DualSOUP", 256*2, 192*2*2, /*SDL_WINDOW_RESIZABLE*/ /* TODO */ 0, &win, &ren))
    {
        printf("window/renderer init failure :(\n");
        return EXIT_FAILURE;
    }

    SDL_AudioSpec audiospec = {SDL_AUDIO_S16LE, 2, SoundMixerOutput};
    SDL_AudioStream* aud = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audiospec, NULL, NULL);
    if (aud == NULL)
    {
        printf("audio init failure :( %s\n", SDL_GetError());
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

    SDL_Texture* blit = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, 256, 192*2);

    SDL_SetTextureScaleMode(blit,
#if SDL_VERSION_ATLEAST(3, 4, 0)
        SDL_SCALEMODE_PIXELART
#else
        SDL_SCALEMODE_NEAREST
#endif
        );

    SDL_Event evts;
    u8* buffer;
    bool buf = false;
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
                    threadexists = false;
                }

                printf("%s\n", ((SDL_DropEvent*)&evts)->data);
                volatile MailBox mailbox = {.rompath = ((SDL_DropEvent*)&evts)->data, .sys = sys, .pad = pad, .aud = aud, .initflag = Init_Busy};
                if (thrd_create(&emu, Core_Init, (void*)&mailbox) != thrd_success)
                {
                    printf("thread init failure :(\n");
                    return EXIT_FAILURE;
                }

                while(mailbox.initflag == Init_Busy);

                if (mailbox.initflag == Init_Fail)
                    break;

                sys = mailbox.sys;

                threadexists = true;

                if (aud != NULL)
                {

                    s16 padding[546*2] = {};
                    if (!SDL_ResumeAudioStreamDevice(aud))
                    {
                        printf("why no turn on?\n");
                    }
                    if (!SDL_PutAudioStreamData(aud, padding, 546*2*2)) printf("%s\n", SDL_GetError());
                }
                break;
            }
            default:
                break;
        }

        if (sys && !sys->Powman.PowerCR.SystemShutDown)
        {
            int ret = mtx_trylock(&sys->FrameBufferMutex[buf]);
            if (ret == thrd_success)
            {
                int pitch; 
                SDL_LockTexture(blit, NULL, (void**)&buffer, &pitch);
                for (int s = 0; s < 2; s++)
                    for (int y = 0; y < 192; y++)
                        for (int x = 0; x < pitch/4; x++)
                            for (int b = 0; b < 4; b++)
                            {
                                if (b == 4) continue;
                                buffer[(s*192*pitch)+(y*pitch)+(x*pitch/256)+b] = (((((sys->Framebuffer[buf][s][y][x] >> (b*6)) & 0x3F) * 0xFF) / 0x3F));
                            }
                SDL_UnlockTexture(blit);
                mtx_unlock(&sys->FrameBufferMutex[buf]);
                buf = !buf;
                SDL_RenderTexture(ren, blit, NULL, NULL);
                SDL_RenderPresent(ren);
                char str[256] = "";
                snprintf(str, 256, "DualSOUP - %f ms - %f ms", sys->FrameTime, sys->FrameTimeActual);
                SDL_SetWindowTitle(win, str);
            }
            // todo: handle thrd_error??? what am i even supposed to do with that information
        }
        else
        {
            threadexists = false;
            SDL_RenderClear(ren);
            SDL_RenderPresent(ren);
            SDL_SetWindowTitle(win, "DualSOUP");
        }

    }
}
