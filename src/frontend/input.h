#pragma once
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_gamepad.h>

#include "core/utils.h"




constexpr u64 MaxBindingsPerInput = 4;

typedef union
{
    struct
    {
        bool Touched;
        u16 X;
        u16 Y;
    };
    u64 Copy;
} TouchCoords;

typedef enum
{
    Input_A,
    Input_B,
    Input_X,
    Input_Y,
    Input_Up,
    Input_Down,
    Input_Left,
    Input_Right,
    Input_Select,
    Input_Start,
    Input_L,
    Input_R,

    CoreInputs_Max [[maybe_unused]] = Input_R+1,
    Input_Max [[maybe_unused]] = Input_R+1,
} DS_Inputs;

constexpr u8 Input_Shift[CoreInputs_Max] =
{
    [Input_A] =         0,
    [Input_B] =         1,
    [Input_X] =         16,
    [Input_Y] =         17,
    [Input_Up] =        6,
    [Input_Down] =      7,
    [Input_Left] =      5,
    [Input_Right] =     4,
    [Input_Select] =    2,
    [Input_Start] =     3,
    [Input_L] =         9,
    [Input_R] =         8,
};

typedef enum
{
    InputType_None,
    InputType_Keyboard,
    InputType_Button,
    InputType_Axis,

    InputType_Max [[maybe_unused]],
} Input_DeviceType;

static const char InputNames[][11] =
{
    [Input_A] =         "A (Right)",
    [Input_B] =         "B (Down)",
    [Input_X] =         "X (Left)",
    [Input_Y] =         "Y (Up)",
    [Input_Up] =        "Dpad Up",
    [Input_Down] =      "Dpad Down",
    [Input_Left] =      "Dpad Left",
    [Input_Right] =     "Dpad Right",
    [Input_Select] =    "Select",
    [Input_Start] =     "Start",
    [Input_L] =         "L Shoulder",
    [Input_R] =         "R Shoulder",
};

typedef struct
{
    Input_DeviceType Type;
    union
    {
        int Typeless;
        SDL_Scancode Scancode;
        struct
        {
            SDL_GamepadAxis Id;
            s16 Threshold;
        } PadAxis;
        SDL_GamepadButton PadButton;
    };
} Core_Input;

u32 Core_PollInputs(SDL_Gamepad* pad, Core_Input* mapping);
