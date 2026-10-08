#include <stdlib.h>
#include <stdio.h>
//#include <locale.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_filesystem.h>

#include "imgui/dcimgui_impl_sdl3.h"

#include "main.h"
#include "gui/maingui.h"
#include "soupparser/soupparser.h"

#include "core/utils.h"
#include "core/console.h"
#include "core/arm/arm9/instr_luts.h"
#include "core/arm/arm7/instr_luts.h"




void Mailbox_UpdateTouch(MailBox* mailbox, u16 x, u16 y, bool touched)
{
    TouchCoords tc = {.X = x, .Y = y, .Touched = touched};
    mailbox->TouchCoords = tc;
}

int SDLCALL Core_Init(void* pass)
{
    MailBox* mailbox = pass;

    // initialize main emulator state struct
    SDL_LockMutex(mailbox->CfgMutex);
    Console* sys = Console_Init((Console*)mailbox->Sys, mailbox->Cfg, mailbox->Aud);
    SDL_UnlockMutex(mailbox->CfgMutex);

    mailbox->Sys = sys;

    if (sys == nullptr)
    {
        mailbox->InitFlag = Init_Fail;
        return EXIT_FAILURE;
    }

    mailbox->InitFlag = Init_Success;

#ifdef USEDIRECTBOOT
    Console_DirectBoot(sys);
#endif

    bool internalkill = false;
    Core_Ret ret = Core_EndFrame;
    while(true)
    {
        bool killchk = false;
        bool pollchk = false;
        bool syncchk = false;
        bool pausechk = false;
        switch(ret)
        {
        case Core_Break:
        {
            pausechk = true;
            killchk = true;
            mailbox->Pause = true;
            break;
        }
        case Core_EndFrame:
        {
            pollchk = true;
            pausechk = true;
            syncchk = true;
            killchk = true;
            break;
        }
        case Core_Poll:
        {
            pollchk = true;
            //syncchk = true;
            break;
        }
        case Core_PowerOff:
        {
            internalkill = true;
            killchk = true;
            break;
        }
        case Core_Continue: unreachable();
        }

        while (pausechk && mailbox->Pause)
        {
            mailbox->PauseConfirm = true;
            if (killchk && mailbox->CoreKill) break; // note: internal kills probably shouldn't override pause
            SDL_Delay(5); // arbitrary delay
        }

        if (killchk && (mailbox->CoreKill || internalkill))
            break;

        if (syncchk) // frame limiter
        {
            double frametimeactual = (double)(SDL_GetPerformanceCounter() - sys->OldTimeActual) * 1000.0 / SDL_GetPerformanceFrequency();
            if (!mailbox->UncapFPS)
            {
                timestamp len = sys->NewSync - sys->LastSync;
                u64 target = sys->OldTime + (((len * SDL_GetPerformanceFrequency()) + sys->TimeFrac) / Sched_Clock);
                sys->TimeFrac =              ((len * SDL_GetPerformanceFrequency()) + sys->TimeFrac) % Sched_Clock;

                while(SDL_GetPerformanceCounter() < target) SDL_CPUPauseInstruction();

                if ((SDL_GetPerformanceCounter() - (SDL_GetPerformanceFrequency() / 60)) > target)
                {
                    sys->OldTime = SDL_GetPerformanceCounter();
                }
                else
                {
                    sys->OldTime = target;
                }

            }
            else sys->OldTime = SDL_GetPerformanceCounter();
            double frametime = (double)(SDL_GetPerformanceCounter() - sys->OldTimeActual) * 1000.0 / SDL_GetPerformanceFrequency();

            sys->LastSync = sys->NewSync;
            if (ret == Core_EndFrame)
            {
                sys->OldTimeActual = SDL_GetPerformanceCounter();
                sys->FrameTime = frametime;
                sys->FrameTimeActual = frametimeactual;
            }

#ifdef FPSLOG
            LogPrint(LOG_ALWAYS, "%lu\n", sys->FrameTime);
#endif
        }

        if (pollchk && Console_TestIfPollingNeeded(sys, sys->NewSync))
        {
            TouchCoords tc = mailbox->TouchCoords;
            sys->TSC.State.X = tc.X;
            sys->TSC.State.Y = tc.Y;
            sys->TSC.State.Touched = tc.Touched;
            sys->InputMain = Input_PollMain(mailbox->Pad);
            sys->InputExtra = Input_PollExtra(tc.Touched, mailbox->Pad);
            sys->LastPoll = sys->NewSync;
        }

        ret = Console_MainLoop(sys);
    }

    if (internalkill)
    {
        SDL_Event evt = {.user = {.type = mailbox->BaseEvent_ID+UserEvent_CorePowerOff}};
        if (!SDL_PushEvent(&evt)) printf("%s\n", SDL_GetError());
    }

    return EXIT_SUCCESS;
}

void CoreThread_Shutdown(MailBox* mailbox, SDL_Thread** cthrd)
{
    if (*cthrd)
    {
        int waity;
        mailbox->CoreKill = true;
        SDL_WaitThread(*cthrd, &waity);
        *cthrd = NULL;
        mailbox->CoreKill = false;
    }
}

void CoreThread_Reset(Console** sys, MailBox* mailbox, SDL_Thread** cthrd)
{
    CoreThread_Shutdown(mailbox, cthrd);

    mailbox->InitFlag = Init_Busy;
    if ((*cthrd = SDL_CreateThread(Core_Init, "SOUP_Core", (void*)mailbox)) == NULL)
    {
        printf("ERROR: thread init failure :( %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    while(mailbox->InitFlag == Init_Busy);

    if (mailbox->InitFlag == Init_Fail)
    {
        int waity;
        SDL_WaitThread(*cthrd, &waity);
    }
    else *sys = (Console*)mailbox->Sys;
}

int main()
{
    //if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL)
    //    printf("could not set character locale\n");

    //SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "X11");

    SDL_SetAppMetadata("DualSOUP", NULL, NULL);

    if (!SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"))
        printf("%s\n", SDL_GetError());
    if (!SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"))
        printf("%s\n", SDL_GetError());
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (!SDL_SetHint(SDL_HINT_AUDIO_DEVICE_RAW_STREAM, "1"))
        printf("%s\n", SDL_GetError());
#endif

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
    SDL_free(path);

    MainCfg mcfg = {.Dirty = false};
    Config_Load(cfgpath, &mcfg, MainCfgData, countof(MainCfgData), &mcfg.Dirty, &mcfg.Mutex);
    Config_Load(NULL, &mcfg.CoreCfg.SysCfg, SystemCfgData, countof(SystemCfgData), NULL, &mcfg.Mutex);
    LogMask = mcfg.LoggingMask;

    MainGUI mgui = MainGUI_Init(&mcfg);

    SDL_AudioSpec audiospec = {SDL_AUDIO_S16LE, 2, SoundMixerOutput};
    SDL_AudioStream* aud = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audiospec, NULL, NULL);
    if (aud == NULL)
    {
        printf("ERROR: SDL Audio open failure :( %s\n", SDL_GetError());
    }

    int numjoy;
    SDL_JoystickID* joysticks = NULL;
    SDL_Gamepad* pad = NULL;

    SDL_Thread* cthrd = NULL;
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
    A9ES_InitInstrLUT();
    T9ES_InitInstrLUT();
    A7TDMI_InitInstrLUT();
    T7TDMI_InitInstrLUT();

    u32 sdlevent_base = SDL_RegisterEvents(UserEvent_MAX);
    mgui.UserEventBase = sdlevent_base;
    MailBox mailbox = {.Sys = sys, .Pad = pad, .Aud = aud, .InitFlag = Init_Busy, .Cfg = &mcfg.CoreCfg, .CfgMutex = mcfg.Mutex, .BaseEvent_ID = sdlevent_base};

    SDL_Event evts;
    while(true)
    {
        while (SDL_PollEvent(&evts))
        {
            cImGui_ImplSDL3_ProcessEvent(&evts);
            switch(evts.type)
            {
            case SDL_EVENT_QUIT:
                CoreThread_Shutdown(&mailbox, &cthrd);
                Console_Cleanup(sys, true);
                return EXIT_SUCCESS;
// TODO: reimplement
#if 0
            case SDL_EVENT_DROP_FILE:
            {
                printf("%s\n", ((SDL_DropEvent*)&evts)->data);
                mcfg.CoreCfg.NTR.CardROM = ((SDL_DropEvent*)&evts)->data;
                CoreThread_Reset(&sys, &mailbox, &cthrd);
                break;
            }
#endif
            case SDL_EVENT_GAMEPAD_ADDED:
            {
                joysticks = SDL_GetGamepads(&numjoy);
                printf("joysticks: %i\n", numjoy);
                if (numjoy) pad = SDL_OpenGamepad(joysticks[0]);
                else pad = NULL;
                SDL_free(joysticks);

                mailbox.Pad = pad;
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED:
            {
                if (!mcfg.GuiCfg.MainWinMaximized)
                {
                    int w;
                    int h;
                    SDL_GetWindowSize(mgui.Win, &w, &h);
                    mcfg.GuiCfg.MainWinHeight = h;
                    mcfg.GuiCfg.MainWinWidth = w;
                    mcfg.Dirty = true;
                }
                break;
            }
            case SDL_EVENT_WINDOW_MOVED:
            {
                if (!mcfg.GuiCfg.MainWinMaximized)
                {
                    int x;
                    int y;
                    SDL_GetWindowPosition(mgui.Win, &x, &y);
                    mcfg.GuiCfg.MainWinX = x;
                    mcfg.GuiCfg.MainWinY = y;
                    mcfg.Dirty = true;
                }
                break;
            }
            case SDL_EVENT_WINDOW_MAXIMIZED:
            {
                mcfg.GuiCfg.MainWinMaximized = true;
                mcfg.Dirty = true;
                break;
            }
            case SDL_EVENT_WINDOW_RESTORED:
            {
                mcfg.GuiCfg.MainWinMaximized = false;
                mcfg.Dirty = true;
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            {
                if (((SDL_KeyboardEvent*)&evts)->scancode == SDL_SCANCODE_F6) // idk
                    mailbox.Pause = !mailbox.Pause;
                break;
            }
            default:
            {
                switch(evts.type-sdlevent_base)
                {
                case UserEvent_CorePowerOff:
                {
                    CoreThread_Shutdown(&mailbox, &cthrd);
                    break;
                }
                case UserEvent_BootRom:
                {
                    CoreThread_Reset(&sys, &mailbox, &cthrd);
                    break;
                }
                }
                break;
            }
            }
        }\

        MainGUI_Loop(sys, &mailbox, &mgui, &mcfg, cthrd != NULL);

        if (mcfg.Dirty)
        {
            Config_Write(cfgpath, &mcfg, MainCfgData, countof(MainCfgData), &mcfg.Dirty, mcfg.Mutex);
        }
    }
}
