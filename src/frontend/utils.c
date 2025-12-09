#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <stdarg.h>
#include "../core/utils.h"
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

// sdl junk
u16 Input_PollMain(void* pad)
{

    u16 inputs = 0;
    if (pad == NULL)
    {
        inputs = 0x03FF;
    }
    else
    {
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
    }
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_APOSTROPHE] << 0);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SEMICOLON] << 1);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_RSHIFT] << 2);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_RETURN] << 3);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_D] << 4);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_A] << 5);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_W] << 6);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_S] << 7);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_LEFTBRACKET] << 8);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_Q] << 9);
    return inputs;
}

u16 Input_PollExtra(void* pad)
{
    u16 inputs = (1<<2) | (1<<4) | (1<<5)
               | (1<<3) // debug
               | (1<<6) // pen
               | (0<<7); // fold
    if (pad == NULL)
    {
        inputs |= 0x003F;
    }
    else
    {
        //inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK) << 3;
        inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH) << 0;
        inputs |= !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST) << 1;
    }
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_P] << 0);
    inputs &= ~(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_L] << 1);
    return inputs;
}

// coroutine junk

volatile bool CR_Start = false;

#ifdef UseThreads
#include <threads.h>
volatile bool CR_Kill = false;

bool CR_Create(thrd_t* handle, void (*func)(void*), void* param)
{
    CR_Start = false;
    CR_Kill = false;
    // 2 MiB is probably way more than needed but idc
    // this cast is almost definitely ub.
    return (thrd_create(handle, (void*)func, param) == thrd_success);
}

void CR_Free(thrd_t handle)
{
    CR_Kill = true;
    int dummy;
    thrd_join(handle, &dummy);
}

void CR_Switch([[maybe_unused]]thrd_t handle)
{
    if (CR_Kill) thrd_exit(1);
}

thrd_t CR_Active()
{
    return (thrd_t)0;
}
#else
#include "../../libs/libco/libco.h"

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

bool CR_Create(cothread_t* handle, void (*func)(void*), void* param)
{
    // 2 MiB is probably way more than needed but idc
    *handle = co_create(((MiB(2)/8)*sizeof(void*)), CR_Boot);
    pass[0] = func;
    pass[1] = param;
    pass[2] = co_active();
    co_switch(*handle);
    return *handle != (cothread_t)0;
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
#endif

