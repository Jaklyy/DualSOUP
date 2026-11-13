#pragma once
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_mutex.h>

#include "core/utils.h"
#include "core/console.h"
#include "input.h"

typedef enum : u8
{
    Init_Busy = 0,
    Init_Success = 1,
    Init_Fail = 2,
} InitFlag;

typedef enum : u32
{
    UserEvent_CorePowerOff,
    UserEvent_BootRom,

    UserEvent_MAX,
} UserEvent_Offsets;

typedef struct MailBox
{
    volatile Console* Sys;
    SDL_Gamepad* Pad;
    SDL_AudioStream* Aud;
    volatile InitFlag InitFlag;
    CoreCfg* Cfg;
    SDL_Mutex* CfgMutex;
    u32 BaseEvent_ID;
    volatile Core_Input* Mappings;
    volatile TouchCoords TouchCoords;
    volatile bool UncapFPS;
    volatile bool Pause;
    volatile bool CfgDisplay;
    volatile bool CoreKill;
    volatile bool CoreDead;
    volatile bool PauseConfirm;
} MailBox;
