#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdarg.h>
#include "../core/utils.h"
#include "../../libs/libco/libco.h"
#include <stdlib.h>




u64 LogMask;

void LogPrint(const u64 logtype, const char* str, ...)
{
#ifndef NOLOGGING
    if ((LogMask & logtype) != logtype) return;

    va_list args;
    va_start(args);

    vprintf(str, args);

    va_end(args);
#endif
}

void CrashSpectacularly(const char* str, ...)
{
    va_list args;
    va_start(args);

    vprintf(str, args);

    va_end(args);

    exit(EXIT_FAILURE);
}

// coroutine junk

// internal variable used to pass along parameters to the new cothread
thread_local void* pass[3];

// internal coroutine startup handler
void CR_Boot()
{
    // im not sure if the compiler is forced to save these...?
    void* func = pass[0];
    void* param = pass[1];

    // we only wanted to initalize the thread we dont need to run it *yet*
    co_switch(pass[2]);

    // jump to saved function using saved parameter
    ((void (*)(void*))func)((void*)param);
}

cothread_t CR_Create(void (*func)(void*), void* param)
{
    // 2 MiB is probably way more than needed but idc
    cothread_t cr = co_create(((MiB(2)/8)*sizeof(void*)), CR_Boot);
    pass[0] = func;
    pass[1] = param;
    pass[2] = co_active();
    co_switch(cr);
    return cr;
}

void CR_Free(cothread_t handle)
{
    co_delete(handle);
}

void CR_Switch(cothread_t handle)
{
    co_switch(handle);
}

cothread_t CR_Active()
{
    return co_active();
}

u16 Input_PollMain(void* pad)
{
    if (pad == NULL)
        return 0x03FF;

    u16 inputs = 0;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) << 0;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH) << 1;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) << 2;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START) << 3;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) << 4;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) << 5;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) << 6;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) << 7;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) << 8;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) << 9;
    return inputs;
}

u16 Input_PollExtra(void* pad)
{
    if (pad == NULL)
        return 0x003F;

    u16 inputs = (1<<2) | (1<<4) | (1<<5)
               | (1<<3) // debug
               | (1<<6) // pen
               | (0<<7); // fold

    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH) << 0;
    inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST) << 1;
    return inputs;
}


