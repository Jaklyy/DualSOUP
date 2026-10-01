#include <stdlib.h>
#include <float.h>

#include "imgui/dcimgui.h"

#include "core/utils.h"
#include "core/console.h"
#include "core/arm/arm9/arm.h"
#include "core/arm/arm7/arm.h"

#include "maingui.h"
#include "../main.h"


#define a9 (&sys->A946ES)
#define cpu9 (&sys->A946ES.ARM)

constexpr ImVec4 A946DEBUG_ITCMCOLOR =  {0, 1, 1, 1};
constexpr ImVec4 A946DEBUG_DTCMCOLOR =  {1, 0, 1, 1};
constexpr ImVec4 A946DEBUG_CACHEHITCOLOR = {1, 1, 0, 1};
constexpr ImVec4 A946DEBUG_CACHEINPROGFILLEDCOLOR = {1, (float).7, (float).5, 1};
constexpr ImVec4 A946DEBUG_CACHEINPROGEMPTYCOLOR = {1, (float).7, 0, 1};
constexpr ImVec4 A946DEBUG_CACHEMISSCOLOR = {1, (float).3, 0, 1};
constexpr ImVec4 A946DEBUG_AHBCOLOR =   {(float).1, (float).8, (float).1, 1};
void DebugGui_PrintRegion(A946DBGREGION var)
{
    ImGui_SameLineEx(0.0, 0.0);
    switch(var)
    {
    case A946DBG_ABORT:                 ImGui_TextColored((ImVec4){1, 0, 0, 1}, "X"); break;
    case A946DBG_PRIV:                  ImGui_TextColored((ImVec4){1, .7, 0, 1}, "P"); break;
    case A946DBG_USER:                  ImGui_TextColored((ImVec4){.4, .7, 1, 1}, "U"); break;
    case A946DBG_NA:                    ImGui_TextDisabled("-"); break;
    case A946DBG_ITCM:                  ImGui_TextColored(A946DEBUG_ITCMCOLOR, "I"); break;
    case A946DBG_DTCM:                  ImGui_TextColored(A946DEBUG_DTCMCOLOR, "D"); break;
    case A946DBG_CACHE_HIT:             ImGui_TextColored(A946DEBUG_CACHEHITCOLOR, "C"); break;
    case A946DBG_CACHE_INPROGFILLED:    ImGui_TextColored(A946DEBUG_CACHEHITCOLOR, "C"); break;
    case A946DBG_CACHE_INPROGEMPTY:     ImGui_TextColored(A946DEBUG_CACHEHITCOLOR, "C"); break;
    case A946DBG_CACHE_MISS:            ImGui_TextColored(A946DEBUG_CACHEHITCOLOR, "C"); break;
    case A946DBG_AHB:                   ImGui_TextColored(A946DEBUG_AHBCOLOR, "A"); break;
    case A946DBG_BUFFERABLE:            ImGui_TextColored((ImVec4){0, 1, 1, 1}, "B"); break;
    case A946DBG_NOBUFFER:              ImGui_TextColored((ImVec4){0, 0.6, .6, 1}, "N"); break;
    }
}

void DebugGui_PushRegionColor(A946DBGREGION var)
{
    switch(var)
    {
    case A946DBG_NA:                    ImGui_PushStyleColor(ImGuiCol_Text, ImGui_GetColorU32(ImGuiCol_TextDisabled)); break;
    case A946DBG_ITCM:                  ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_ITCMCOLOR); break;
    case A946DBG_DTCM:                  ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_DTCMCOLOR); break;
    case A946DBG_CACHE_HIT:             ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_CACHEHITCOLOR); break;
    case A946DBG_CACHE_INPROGFILLED:    ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_CACHEINPROGFILLEDCOLOR); break;
    case A946DBG_CACHE_INPROGEMPTY:     ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_CACHEINPROGEMPTYCOLOR); break;
    case A946DBG_CACHE_MISS:            ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_CACHEMISSCOLOR); break;
    case A946DBG_AHB:                   ImGui_PushStyleColorImVec4(ImGuiCol_Text, A946DEBUG_AHBCOLOR); break;
    default: CrashSpectacularly("jakly you idiot\n");
    }
}

void DebugGui_BusWatch(Bus_Breakpoint list[const Bus_DebugMaxWatch], int* num)
{
    ImGui_InputIntEx("Num", num, 1, 1, 0);
    if (ImGui_BeginTable("tablebuswatch", 8,
        ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Borders
        | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui_TableSetupColumn("Min Address", 0);
        ImGui_TableSetupColumn("Max Address", 0);
        ImGui_TableSetupColumn("Manager Mask", 0);
        ImGui_TableSetupColumn("Write Value", 0);
        ImGui_TableSetupColumn("Write Match", 0);
        ImGui_TableSetupColumn("Width Mask", 0);
        ImGui_TableSetupColumn("Write", 0);
        ImGui_TableSetupColumn("Read", 0);
        ImGui_TableHeadersRow();
        for (u64 i = 0; i < Bus_DebugMaxWatch; i++)
        {
            ImGui_PushIDInt(i);
            ImGui_TableNextRow();
            if (i % 2) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBg), -1);
            else       ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBgAlt), -1);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##min", &list[i].AddrMin, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##max", &list[i].AddrMax, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##man", &list[i].ManMask, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##val", &list[i].WrData, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##match", &list[i].WriteMatch, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##wid", &list[i].WidthMask, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_Checkbox("##mwr", &list[i].MustWrite);
            ImGui_TableNextColumn();
            ImGui_Checkbox("##mrd", &list[i].MustRead);
            ImGui_PopID();
        }
        ImGui_EndTable();
    }
}

void DebugGui_A9(MailBox* mail, DebugGui* dgui, Console* sys)
{
    ImGui_SetNextWindowSize((ImVec2){725, 348}, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("ARM9 Debugger", &dgui->A9DbgDisplay, 0))
    {
        if (sys)
        {
            if (ImGui_Button("Jump To A9 PC"))
            {
                dgui->CurAddr9 = cpu9->CurExec;
                sprintf(dgui->AddrText9, "%08X", dgui->CurAddr9);
            }

            ImGui_SameLine();
            ImGui_SetNextItemWidth(65.0);
            ImGui_InputText("Jump To", dgui->AddrText9, sizeof(dgui->AddrText9), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
            if (ImGui_IsItemDeactivatedAfterEdit())
            {
                dgui->CurAddr9 = strtoul(dgui->AddrText9, NULL, 16);
            }
            ImGui_SameLine();
            if (ImGui_Button("Step Once"))
            {
                cpu9->StepOnce = true;
                mail->Pause = false;
            }
            ImGui_SameLine();
            if (ImGui_Button("Step Over"))
            {
                cpu9->StepOver = true;
                cpu9->ExecBreak = cpu9->CurExec + (cpu9->CPSR.Thumb ? 2 : 4);
                mail->Pause = false;
            }

            if (ImGui_BeginTable("tablea9view", 6,
                ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Borders
                | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui_TableSetupColumn("Perms+Mem", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("A9 IBus", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("A9 DBus", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("A9 AHB", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Disassembly", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableHeadersRow();

                for (int i = 0; i < 16; i++)
                {
                    u32 addr = dgui->CurAddr9 - 8 + (4*i);
                    ImGui_TableNextRow();
                    if (addr == cpu9->CurExec) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0xFF1F6F6F, -1);
                    else
                    {
                        if (i % 2) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBg), -1);
                        else       ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBgAlt), -1);
                    }
                    ImGui_TableNextColumn();

                    A946_DebugRegion reg = A946_DebugGetRegion(a9, addr);
                    ImGui_Text("R:");
                    DebugGui_PrintRegion(reg.RPerm);
                    DebugGui_PrintRegion(reg.RReg);
                        ImGui_SameLineEx(0.0, 0.0); ImGui_Text(" W:");
                    DebugGui_PrintRegion(reg.WPerm);
                    DebugGui_PrintRegion(reg.WReg);
                    DebugGui_PrintRegion(reg.WBuff);
                        ImGui_SameLineEx(0.0, 0.0); ImGui_Text(" X:");
                    DebugGui_PrintRegion(reg.XPerm);
                    DebugGui_PrintRegion(reg.XReg);

                    ImGui_TableNextColumn();
                    ImGui_Text("%08"PRIX32"", addr);
                    ImGui_TableNextColumn();
                    DebugGui_PushRegionColor(reg.XReg);
                    ImGui_Text("%08"PRIX32"", A946_DebugInstrRead(a9, addr));
                    ImGui_PopStyleColor();
                    ImGui_TableNextColumn();
                    DebugGui_PushRegionColor(reg.RReg);
                    ImGui_Text("%08"PRIX32"", A946_DebugDataRead(a9, addr));
                    ImGui_PopStyleColor();
                    ImGui_TableNextColumn();
                    ImGui_Text("%08"PRIX32"", Bus9_DebugRead(sys, addr));
                    ImGui_TableNextColumn();
                    ImGui_Text("BL");
                    ImGui_SameLine();
                    ImGui_TextColored((ImVec4){1, 0.8, 0.9, 1}, "0x02000000");
                }
                ImGui_EndTable();
            }

            ImGui_SameLine();
            if (ImGui_BeginChild("ARM946E-S View", (ImVec2){230, 170}, ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX|ImGuiChildFlags_ResizeY, 0))
            {
                for (int i = 0; i < 10; i++)
                {
                    ImGui_Text("R%i:   %08"PRIX32, i, cpu9->R[i]);
                    i++;
                    ImGui_SameLine();
                    ImGui_Text("R%i:   %08"PRIX32, i, cpu9->R[i]);
                }
                for (int i = 10; i < 16; i++)
                {
                    ImGui_Text("R%i:  %08"PRIX32, i, cpu9->R[i]);
                    i++;
                    ImGui_SameLine();
                    ImGui_Text("R%i:  %08"PRIX32, i, cpu9->R[i]);
                }
                ImGui_Text("CPSR: %08"PRIX32, cpu9->CPSR.Raw);
                ImGui_SameLine();
                ImGui_BeginDisabled(!A9ES_HasSPSR(a9));
                ImGui_Text("SPSR: %08"PRIX32, A9ES_GetSPSR(a9).Raw);
                ImGui_EndDisabled();
            }
            ImGui_EndChild();
            DebugGui_BusWatch(sys->Bus9_Watch, &sys->Bus9_NumWatch);
            ImGui_SameLine();
            if (ImGui_BeginChild("Interrupts", (ImVec2){230, 170}, ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX|ImGuiChildFlags_ResizeY, 0))
            {
                ImGui_Text("IE: %08"PRIX32, sys->IE9);
                ImGui_Text("IF: %08"PRIX32, sys->IF9);
                ImGui_Text("IME: %i", sys->IME9);
            }
            ImGui_EndChild();
        }
    }
    ImGui_End();
}
#undef a9
#undef cpu9
#define a7 (&sys->A7TDMI)
#define cpu7 (&sys->A7TDMI.ARM)

void DebugGui_A7(MailBox* mail, DebugGui* dgui, Console* sys)
{
    ImGui_SetNextWindowSize((ImVec2){470, 348}, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("ARM7 Debugger", &dgui->A7DbgDisplay, 0))
    {
        if (sys)
        {
            if (ImGui_Button("Jump To A7 PC"))
            {
                dgui->CurAddr7 = cpu7->CurExec;
                sprintf(dgui->AddrText7, "%08X", dgui->CurAddr7);
            }

            ImGui_SameLine();
            ImGui_SetNextItemWidth(65.0);
            ImGui_InputText("Jump To", dgui->AddrText7, sizeof(dgui->AddrText7), ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
            if (ImGui_IsItemDeactivatedAfterEdit())
            {
                dgui->CurAddr7 = strtoul(dgui->AddrText7, NULL, 16);
            }
            ImGui_SameLine();
            if (ImGui_Button("Step Once"))
            {
                cpu7->StepOnce = true;
                mail->Pause = false;
            }
            ImGui_SameLine();
            if (ImGui_Button("Step Over"))
            {
                cpu7->StepOver = true;
                cpu7->ExecBreak = cpu7->CurExec + (cpu7->CPSR.Thumb ? 2 : 4);
                mail->Pause = false;
            }

            if (ImGui_BeginTable("tablea7view", 3,
                ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Borders
                | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui_TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Bus View", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Disassembly", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableHeadersRow();

                for (int i = 0; i < 16; i++)
                {
                    u32 addr = dgui->CurAddr7 - 8 + (4*i);
                    ImGui_TableNextRow();
                    if (addr == cpu7->CurExec) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0xFF1F6F6F, -1);
                    else
                    {
                        if (i % 2) ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBg), -1);
                        else       ImGui_TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui_GetColorU32(ImGuiCol_TableRowBgAlt), -1);
                    }
                    ImGui_TableNextColumn();
                    ImGui_Text("%08"PRIX32"", addr);
                    ImGui_TableNextColumn();
                    ImGui_Text("%08"PRIX32"", Bus7_DebugRead(sys, addr));
                    ImGui_TableNextColumn();
                    ImGui_Text("BL");
                    ImGui_SameLineEx(0, 0);
                    ImGui_TextColored((ImVec4){1, 0.8, 0.9, 1}, "0x02000000");
                }
                ImGui_EndTable();
            }

            ImGui_SameLine();
            if (ImGui_BeginChild("ARM7TDMI View", (ImVec2){230, 170}, ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX|ImGuiChildFlags_ResizeY, 0))
            {
                for (int i = 0; i < 10; i++)
                {
                    ImGui_Text("R%i:   %08X", i, cpu7->R[i]);
                    i++;
                    ImGui_SameLine();
                    ImGui_Text("R%i:   %08X", i, cpu7->R[i]);
                }
                for (int i = 10; i < 16; i++)
                {
                    ImGui_Text("R%i:  %08X", i, cpu7->R[i]);
                    i++;
                    ImGui_SameLine();
                    ImGui_Text("R%i:  %08X", i, cpu7->R[i]);
                }
                ImGui_Text("CPSR: %08X", cpu7->CPSR.Raw);
                ImGui_SameLine();
                ImGui_BeginDisabled(!A7TDMI_HasSPSR(a7));
                ImGui_Text("SPSR: %08X", A7TDMI_GetSPSR(a7).Raw);
                ImGui_EndDisabled();
            }
            ImGui_EndChild();
            DebugGui_BusWatch(sys->Bus7_Watch, &sys->Bus7_NumWatch);
            ImGui_SameLine();
            if (ImGui_BeginChild("Interrupts", (ImVec2){230, 170}, ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX|ImGuiChildFlags_ResizeY, 0))
            {
                ImGui_Text("IE: %08"PRIX64, sys->IE7);
                ImGui_Text("IF: %08"PRIX64, sys->IF7);
                ImGui_Text("IME: %i", sys->IME7);
            }
            ImGui_EndChild();
        }
    }
    ImGui_End();
}
#undef a7
#undef cpu7

void DebugGui_Sched(DebugGui* dgui, Console* sys)
{
    ImGui_SetNextWindowSize((ImVec2){700, 348}, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("Scheduler Debugger", &dgui->Sched, 0))
    {
        if (sys)
        {
            if (ImGui_BeginTable("Active", 2,
                ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Borders
                | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui_TableSetupColumn("Evt ID", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableHeadersRow();

                Sched* sched = &sys->Sched;
                Scheduler_Events evt = Evt_Null;
                do
                {
                    evt = sched->Next[evt];
                    ImGui_TableNextRow();
                    ImGui_TableNextColumn();
                    ImGui_Text("%3"PRIi8, evt);
                    ImGui_TableNextColumn();
                    ImGui_Text("%16"PRIX64, sched->Times[evt]);
                } while(sched->Next[evt] != Evt_Invalid);
                ImGui_EndTable();
            }
            ImGui_SameLine();
            if (ImGui_BeginTable("Full", 4,
                ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Borders
                | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui_TableSetupColumn("Evt ID", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Prev", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Next", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableHeadersRow();

                Sched* sched = &sys->Sched;
                for (Scheduler_Events evt = 0; evt < Evt_Max; evt++)
                {
                    ImGui_TableNextRow();
                    ImGui_TableNextColumn();
                    ImGui_Text("%3"PRIi8, evt);
                    ImGui_TableNextColumn();
                    ImGui_Text("%16"PRIX64, sched->Times[evt]);
                    ImGui_TableNextColumn();
                    ImGui_Text("%3"PRIi8, sched->Prev[evt]);
                    ImGui_TableNextColumn();
                    ImGui_Text("%3"PRIi8, sched->Next[evt]);
                }
                ImGui_EndTable();
            }
            ImGui_SameLine();
            if (ImGui_BeginTable("Misc", 2,
                ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Borders
                | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui_TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
                ImGui_TableHeadersRow();

                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                ImGui_Text("SWRenTimestamp");
                ImGui_TableNextColumn();
                ImGui_Text("%16"PRIX64, sys->SWRenTimestamp);
                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                ImGui_Text("SWRenTarget");
                ImGui_TableNextColumn();
                ImGui_Text("%16"PRIX64, sys->SWRenTarget);

                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                ImGui_Text("PPUATimestamp");
                ImGui_TableNextColumn();
                ImGui_Text("%16"PRIX64, sys->PPUATimestamp);
                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                ImGui_Text("PPUBTimestamp");
                ImGui_TableNextColumn();
                ImGui_Text("%16"PRIX64, sys->PPUBTimestamp);
                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                ImGui_Text("PPUTarget");
                ImGui_TableNextColumn();
                ImGui_Text("%16"PRIX64, sys->PPUTarget);
                ImGui_EndTable();
            }
        }
    }
    ImGui_End();
}

void DebugGUI_Loop(MailBox* mail, MainGUI* mgui, Console* sys)
{
    if (mgui->dbg.A9DbgDisplay) DebugGui_A9(mail, &mgui->dbg, sys);
    if (mgui->dbg.A7DbgDisplay) DebugGui_A7(mail, &mgui->dbg, sys);
    if (mgui->dbg.Sched) DebugGui_Sched(&mgui->dbg, sys);
}
