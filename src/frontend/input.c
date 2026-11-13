#include <SDL3/SDL_keyboard.h>

#include "input.h"




u32 Core_PollInputs(SDL_Gamepad* pad, Core_Input* mappings)
{
    u32 outKeys = 0x007F03FF;
    int numKeys;
    const bool* keys = SDL_GetKeyboardState(&numKeys);
    for (u64 i = 0; i < CoreInputs_Max; i++)
    {
        for (u64 j = 0; j < MaxBindingsPerInput; j++)
        {
            Core_Input* map = &mappings[(i * MaxBindingsPerInput) + j];
            switch(map->Type)
            {
            case InputType_None: break;
            case InputType_Keyboard:
            {
                if (map->Scancode >= (unsigned)numKeys) break; // invalid key?

                outKeys &= ~((u32)keys[map->Scancode] << Input_Shift[i]);
                break;
            }
            case InputType_Button:
            {
                if (map->PadButton >= SDL_GAMEPAD_BUTTON_COUNT) break; // invalid button?

                outKeys &= ~((u32)SDL_GetGamepadButton(pad, map->PadButton) << Input_Shift[i]);
                break;
            }
            case InputType_Axis:
            {
                if (map->PadAxis.Id >= SDL_GAMEPAD_AXIS_COUNT) break; // invalid axis?

                s16 axis = SDL_GetGamepadAxis(pad, map->PadAxis.Id);
                s16 thresh = map->PadAxis.Threshold;

                if (thresh < 0 && (axis <= thresh)) outKeys &= ~((u32)1 << Input_Shift[i]);
                else if (thresh >= 0 && (axis >= thresh)) outKeys &= ~((u32)1 << Input_Shift[i]);
                break;
            }
            }
        }
    }

    // prevent L+R and U+D
    // TODO: make configurable
    if (!(outKeys & (1<<Input_Shift[Input_Left])))
        outKeys |= (1<<Input_Shift[Input_Right]);
    if (!(outKeys & (1<<Input_Shift[Input_Up])))
        outKeys |= (1<<Input_Shift[Input_Down]);

    return outKeys;
}
