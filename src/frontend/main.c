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

#include "../../libs/imgui/dcimgui_impl_sdl3.h"

#include "gui/maingui.h"
#include "soupparser/soupparser.h"

#include "../core/utils.h"
#include "../core/console.h"
#include "../core/arm/arm9/instr_luts.h"
#include "../core/arm/arm7/instr_luts.h"




typedef enum : u8
{
    Init_Busy = 0,
    Init_Success = 1,
    Init_Fail = 2,
} InitFlag;

typedef struct
{
    volatile struct Console* sys;
    SDL_Gamepad* pad;
    SDL_AudioStream* aud;
    volatile InitFlag initflag;
    CoreCfg cfg;
} MailBox;

int SDLCALL Core_Init(void* pass)
{
    MailBox* mailbox = pass;

    // initialize main emulator state struct
    struct Console* sys = Console_Init((struct Console*)mailbox->sys, mailbox->cfg, mailbox->pad, mailbox->aud);
    if (sys == nullptr)
    {
        mailbox->initflag = Init_Fail;
        return EXIT_FAILURE;
    }

    mailbox->sys = sys;
    mailbox->initflag = Init_Success;

#ifdef USEDIRECTBOOT
    Console_DirectBoot(sys);
#endif
    Console_MainLoop(sys);

    sys->KillThread = false;

    return EXIT_SUCCESS;
}

void CoreThread_Reset(struct Console** sys, SDL_Thread** thrd, SDL_Gamepad* pad, SDL_AudioStream* aud, const CoreCfg cfg, bool* frontbuffer, bool* thrdrunning)
{
    if (*thrdrunning)
    {
        (*sys)->KillThread = true;
        while((*sys)->KillThread);
        *thrdrunning = false;
    }

    MailBox mailbox = {.sys = *sys, .pad = pad, .aud = aud, .initflag = Init_Busy, .cfg = cfg};
    if (!*thrdrunning && ((*thrd = SDL_CreateThread(Core_Init, "SOUP_Core", (void*)&mailbox)) == NULL))
    {
        printf("ERROR: thread init failure :( %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    while(mailbox.initflag == Init_Busy);

    if (mailbox.initflag == Init_Fail)
        return;

    *sys = (struct Console*)mailbox.sys;

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

    MainGUI mgui = MainGUI_Init();

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
    struct Console* sys = nullptr;

    char* path = SDL_GetPrefPath("DualSOUP", "DualSOUP");
    constexpr char ininame[] = "DualSOUP.ini";
    char* cfgpath = malloc(strlen(path)+sizeof(ininame));
    strcpy(cfgpath, path);
    strcat(cfgpath, ininame);

    CoreCfg mcfg = {.Dirty = false};
    Config_Load(cfgpath, &mcfg, MainCfg, sizeof(MainCfg)/sizeof(MainCfg[0]), &mcfg.Dirty, &mcfg.Mutex);
    Config_Load(NULL, &mcfg.SysCfg, SystemCfg, sizeof(SystemCfg)/sizeof(SystemCfg[0]), NULL, &mcfg.Mutex);

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
                    return EXIT_SUCCESS;
                case SDL_EVENT_DROP_FILE:
                {
                    printf("%s\n", ((SDL_DropEvent*)&evts)->data);                    
                    mcfg.NTR.CardROM = ((SDL_DropEvent*)&evts)->data;
                    CoreThread_Reset(&sys, &cthrd, pad, aud, mcfg, &mgui.Buffer, &thrdrunning);
                    break;
                }
                default:
                    break;
            }
        }

        thrdrunning = MainGUI_Loop(sys, &mgui, &mcfg);

        if (mcfg.Dirty)
        {
            Config_Write(cfgpath, &mcfg, MainCfg, sizeof(MainCfg)/sizeof(MainCfg[0]), &mcfg.Dirty, mcfg.Mutex);
        }
    }
}
