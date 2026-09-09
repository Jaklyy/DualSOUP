#include <SDL3/SDL_mutex.h>
#include <stdlib.h>
#include <stdio.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_filesystem.h>

#include "imgui/dcimgui_impl_sdl3.h"

#include "gui/maingui.h"
#include "soupparser/soupparser.h"

#include "core/utils.h"
#include "core/console.h"
#include "core/arm/arm9/instr_luts.h"
#include "core/arm/arm7/instr_luts.h"




typedef enum : u8
{
    Init_Busy = 0,
    Init_Success = 1,
    Init_Fail = 2,
} InitFlag;

typedef struct
{
    volatile Console* Sys;
    SDL_Gamepad* Pad;
    SDL_AudioStream* Aud;
    volatile InitFlag InitFlag;
    CoreCfg* Cfg;
    SDL_Mutex* CfgMutex;
} MailBox;

int SDLCALL Core_Init(void* pass)
{
    MailBox* mailbox = pass;

    // initialize main emulator state struct
    SDL_LockMutex(mailbox->CfgMutex);
    Console* sys = Console_Init((Console*)mailbox->Sys, mailbox->Cfg, mailbox->Pad, mailbox->Aud);
    SDL_UnlockMutex(mailbox->CfgMutex);
    if (sys == nullptr)
    {
        mailbox->InitFlag = Init_Fail;
        return EXIT_FAILURE;
    }

    mailbox->Sys = sys;
    mailbox->InitFlag = Init_Success;

#ifdef USEDIRECTBOOT
    Console_DirectBoot(sys);
#endif
    Console_MainLoop(sys);

    sys->KillThread = false;

    return EXIT_SUCCESS;
}

void CoreThread_Shutdown(volatile Console* sys, bool* thrdrunning)
{
    if (*thrdrunning)
    {
        sys->KillThread = true;
        while(sys->KillThread); // todo: add timeout
        *thrdrunning = false;
    }
}

void CoreThread_Reset(Console** sys, SDL_Thread** thrd, SDL_Gamepad* pad, SDL_AudioStream* aud, CoreCfg* cfg, SDL_Mutex* cfgmutex, bool* frontbuffer, bool* thrdrunning)
{
    CoreThread_Shutdown(*sys, thrdrunning);

    MailBox mailbox = {.Sys = *sys, .Pad = pad, .Aud = aud, .InitFlag = Init_Busy, .Cfg = cfg, .CfgMutex = cfgmutex};
    if (!*thrdrunning && ((*thrd = SDL_CreateThread(Core_Init, "SOUP_Core", (void*)&mailbox)) == NULL))
    {
        printf("ERROR: thread init failure :( %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    while(mailbox.InitFlag == Init_Busy);

    if (mailbox.InitFlag == Init_Fail)
        return;

    *sys = (Console*)mailbox.Sys;

    *frontbuffer = false; // feels wrong to be resetting this here...?
    *thrdrunning = true;
    return;
}

int main()
{
    LogMask = u64_max; // temp

    SDL_SetAppMetadata("DualSOUP", NULL, NULL);

    if (!SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"))
        printf("%s\n", SDL_GetError());
    if (!SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"))
        printf("%s\n", SDL_GetError());
    if (!SDL_SetHint(SDL_HINT_AUDIO_DEVICE_RAW_STREAM, "1"))
        printf("%s\n", SDL_GetError());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_AUDIO))
    {
        printf("SDL_Init Error!!! %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    atexit(SDL_Quit); // apparently this is a thing i should be doing.

    char* path = SDL_GetPrefPath("DualSOUP", "DualSOUP");
    constexpr char ininame[] = "DualSOUP.ini";
    char* cfgpath = malloc(strlen(path)+sizeof(ininame));
    strcpy(cfgpath, path);
    strcat(cfgpath, ininame);

    MainCfg mcfg = {.Dirty = false};
    Config_Load(cfgpath, &mcfg, MainCfgData, countof(MainCfgData), &mcfg.Dirty, &mcfg.Mutex);
    Config_Load(NULL, &mcfg.CoreCfg.SysCfg, SystemCfgData, countof(SystemCfgData), NULL, &mcfg.Mutex);

    MainGUI mgui = MainGUI_Init(&mcfg);

    SDL_AudioSpec audiospec = {SDL_AUDIO_S16LE, 2, SoundMixerOutput};
    SDL_AudioStream* aud = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audiospec, NULL, NULL);
    if (aud == NULL)
    {
        printf("ERROR: SDL Audio open failure :( %s\n", SDL_GetError());
    }

    int num;
    SDL_JoystickID* joysticks = SDL_GetGamepads(&num);
    printf("joysticks: %i\n", num);

    SDL_Gamepad* pad = NULL;
    if (num)
    {
        pad = SDL_OpenGamepad(joysticks[0]);
    }

    bool thrdrunning = false;
    SDL_Thread* cthrd;
    Console* sys = nullptr;

    if (aud != NULL)
    {
        if (!SDL_ResumeAudioStreamDevice(aud))
        {
            printf("ERROR: why audio no turn on? %s\n", SDL_GetError());
        }
    }

    // TODO: do this at compile time?
    // init arm luts
    ARM9_InitInstrLUT();
    THUMB9_InitInstrLUT();
    ARM7_InitInstrLUT();
    THUMB7_InitInstrLUT();

    SDL_Event evts;
    while(true)
    {
        while (SDL_PollEvent(&evts))
        {
            cImGui_ImplSDL3_ProcessEvent(&evts);
            switch(evts.type)
            {
                case SDL_EVENT_QUIT:
                    CoreThread_Shutdown(sys, &thrdrunning);
                    return EXIT_SUCCESS;
                case SDL_EVENT_DROP_FILE:
                {
                    printf("%s\n", ((SDL_DropEvent*)&evts)->data);
                    mcfg.CoreCfg.NTR.CardROM = ((SDL_DropEvent*)&evts)->data;
                    CoreThread_Reset(&sys, &cthrd, pad, aud, &mcfg.CoreCfg, mcfg.Mutex, &mgui.Buffer, &thrdrunning);
                    break;
                }
                default:
                    break;
            }
        }

        thrdrunning = MainGUI_Loop(sys, &mgui, &mcfg);

        if (mcfg.Dirty)
        {
            Config_Write(cfgpath, &mcfg, MainCfgData, countof(MainCfgData), &mcfg.Dirty, mcfg.Mutex);
        }
    }
}
