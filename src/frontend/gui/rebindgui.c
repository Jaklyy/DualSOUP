#include <stdlib.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>

#include "imgui/dcimgui.h"

#include "frontend/input.h"
#include "frontend/soupparser/soupparser.h"
#include "core/utils.h"





void RebindGui_Loop(s64* curRebind, MainCfg* mcfg)
{
    Core_Input* mappings = mcfg->InputCfg;
    if (ImGui_BeginTable("InputBindingTable", 1+MaxBindingsPerInput, ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders))
    {
        ImGui_TableSetupColumn("Input", 0);
        for (u64 i = 0; i < MaxBindingsPerInput; i++)
        {
            ImGui_PushIDInt(i);
            ImGui_TableSetupColumn("", 0);
            ImGui_PopID();
        }
        ImGui_TableHeadersRow();
        for (size_t i = 0; i < Input_Max; i++)
        {
            ImGui_PushIDInt(i);
            ImGui_TableNextRow();
            ImGui_TableNextColumn();

            ImGui_TextUnformatted(InputNames[i]);

            for (u64 j = 0; j < MaxBindingsPerInput; j++)
            {
                ImGui_PushIDInt(j);
                ImGui_TableNextColumn();
                s64 curOff = (i * MaxBindingsPerInput) + j;
                Core_Input* map = &mappings[curOff];
                switch(map->Type)
                {
                case InputType_None:
                {
                    if (ImGui_SelectableEx("Unbound", *curRebind == curOff, 0, (ImVec2){0, 0}))
                    {
                        if (*curRebind == curOff) *curRebind = -1;
                        else *curRebind = curOff;
                    }
                    break;
                }
                case InputType_Keyboard:
                {
                    if (ImGui_SelectableEx(SDL_GetScancodeName(map->Scancode), *curRebind == curOff, 0, (ImVec2){0, 0}))
                    {
                        if (*curRebind == curOff) *curRebind = -1;
                        else *curRebind = curOff;
                    }
                    break;
                }
                case InputType_Button:
                {
                    if (ImGui_SelectableEx(SDL_GetGamepadStringForButton(map->PadButton), *curRebind == curOff, 0, (ImVec2){0, 0}))
                    {
                        if (*curRebind == curOff) *curRebind = -1;
                        else *curRebind = curOff;
                    }
                    break;
                }
                case InputType_Axis:
                {
                    const char* str = SDL_GetGamepadStringForAxis(map->PadAxis.Id);
                    char newstr[strlen(str)+2];
                    strcpy(newstr, str);
                    if (map->PadAxis.Threshold < 0) strcat(newstr, "-");
                    else strcat(newstr, "+");

                    if (ImGui_SelectableEx(newstr, *curRebind == curOff, 0, (ImVec2){0, 0}))
                    {
                        if (*curRebind == curOff) *curRebind = -1;
                        else *curRebind = curOff;
                    }
                    break;
                }
                }
                ImGui_PopID();
            }
            ImGui_PopID();
        }
        ImGui_EndTable();
    }
    ImGui_BeginDisabled(*curRebind == -1);
    if (ImGui_Button("Clear Binding"))
    {
        SDL_LockMutex(mcfg->Mutex);
        mappings[*curRebind].Type = InputType_None;
        mappings[*curRebind].Typeless = 0;
        mappings[*curRebind].PadAxis.Threshold = 0;
        *curRebind = -1;
        mcfg->Dirty = true;
        SDL_UnlockMutex(mcfg->Mutex);
    }
    ImGui_EndDisabled();
}