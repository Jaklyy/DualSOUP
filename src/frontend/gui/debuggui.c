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
        ImGui_TableSetupColumn("Match Data", 0);
        ImGui_TableSetupColumn("Match Mask", 0);
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
            ImGui_InputIntEx("##val", &list[i].DataToMatch, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui_TableNextColumn();
            ImGui_PushItemWidth(-FLT_MIN); // Right-aligned
            ImGui_InputIntEx("##match", &list[i].MaskForMatch, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
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

void DebugGui_ReadInstr(u32 val, u32 addr, bool thumb)
{
    if (thumb) ImGui_Text("%04"PRIX16"", ROR32(val, 8*(addr&2)) & 0xFFFF);
    else       ImGui_Text("%08"PRIX32"", val);
}

void DebugGui_A9(MailBox* mail, DebugGui* dgui, Console* sys)
{
    ImGui_SetNextWindowSize((ImVec2){725, 348}, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("ARM9 Debugger", &dgui->A9DbgDisplay, 0))
    {
        if (sys)
        {
            bool update = dgui->FollowA9 | ImGui_Button("Jump To A9 PC");
            ImGui_SameLine();
            ImGui_Checkbox("Follow PC", &dgui->FollowA9);
            if (update)
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

                bool thumb = cpu9->CPSR.Thumb;
                for (int i = 0; i < 16; i++)
                {
                    u32 addr = dgui->CurAddr9 - ((8-i) * (thumb ? 2 : 4));
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
                    DebugGui_ReadInstr(A946_DebugInstrRead(a9, addr), addr, thumb);
                    ImGui_PopStyleColor();
                    ImGui_TableNextColumn();
                    DebugGui_PushRegionColor(reg.RReg);
                    DebugGui_ReadInstr(A946_DebugDataRead(a9, addr), addr, thumb);
                    ImGui_PopStyleColor();
                    ImGui_TableNextColumn();
                    DebugGui_ReadInstr(Bus9_DebugRead(sys, addr), addr, thumb);
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

            ImGui_SetNextItemWidth(65.0);
            ImGui_InputIntEx("Break Addr", &dgui->A9BreakAddr, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
            ImGui_SameLine();
            if (ImGui_Checkbox("Break On Exec", &dgui->A9ExecBreak))
            {
                if (dgui->A9ExecBreak)
                {
                    cpu9->StepOver = false;
                    cpu9->ExecBreak = dgui->A9BreakAddr;
                }
                else cpu9->ExecBreak = u64_max;
            }
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
            bool update = dgui->FollowA7 | ImGui_Button("Jump To A7 PC");
            ImGui_SameLine();
            ImGui_Checkbox("Follow PC", &dgui->FollowA7);
            if (update)
            {
                dgui->CurAddr7 = cpu7->CurExec;
                sprintf(dgui->AddrText7, "%08"PRIX32, dgui->CurAddr7);
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

                bool thumb = cpu7->CPSR.Thumb;
                for (int i = 0; i < 16; i++)
                {
                    u32 addr = dgui->CurAddr7 - ((8-i) * (thumb ? 2 : 4));
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
                    DebugGui_ReadInstr(Bus7_DebugRead(sys, addr), addr, thumb);
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

            ImGui_SetNextItemWidth(65.0);
            ImGui_InputIntEx("Break Addr", &dgui->A7BreakAddr, 0, 0, ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase);
            ImGui_SameLine();
            if (ImGui_Checkbox("Break On Exec", &dgui->A7ExecBreak))
            {
                if (dgui->A7ExecBreak)
                {
                    cpu7->StepOver = false;
                    cpu7->ExecBreak = dgui->A7BreakAddr;
                }
                else cpu7->ExecBreak = u64_max;
            }
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
                    ImGui_Text("%s", Scheduler_Events_Names[evt]);
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
                    ImGui_Text("%s", Scheduler_Events_Names[evt]);
                    ImGui_TableNextColumn();
                    ImGui_Text("%16"PRIX64, sched->Times[evt]);
                    ImGui_TableNextColumn();
                    ImGui_Text("%s", Scheduler_Events_Names[sched->Prev[evt]]);
                    ImGui_TableNextColumn();
                    ImGui_Text("%s", Scheduler_Events_Names[sched->Next[evt]]);
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

typedef enum : u8 {
    DEBUGGER_DMAMODE_AGB,
    DEBUGGER_DMAMODE_NTR7,
    DEBUGGER_DMAMODE_NTR9,
} DEBUGGER_DMAMODE;

void DebugGui_DMA(DMA_Channel dma[4], const char* const name, DEBUGGER_DMAMODE mode)
{
    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
    if (ImGui_TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
    {
        for (int i = 0; i < 4; i++)
        {
            constexpr char ChannelNames[][10] = {"Channel 0", "Channel 1", "Channel 2", "Channel 3"};
            if (ImGui_TreeNodeEx(ChannelNames[i], ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
            {
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Src: %08"PRIX32, dma[i].SrcAddr);
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Dst: %08"PRIX32, dma[i].DstAddr);
                if (ImGui_TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
                {
                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        ImGui_Text("Num Words: %"PRIu32, dma[i].CR.NumWords);
                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        constexpr char DstModes[][11] = {"Inc", "Dec", "Fixed", "Inc+Reload"};
                        ImGui_Text("Dst Mode: %s", DstModes[dma[i].CR.DestCR]);
                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        constexpr char SrcModes[][11] = {"Inc", "Dec", "Fixed", "Prohibited"};
                        ImGui_Text("Dst Mode: %s", SrcModes[dma[i].CR.SourceCR]);
                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        ImGui_Text("Repeat: %s", dma[i].CR.Repeat ? "Enabled" : "Disabled");
                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        ImGui_Text("Width: %s", dma[i].CR.Width32 ? "32 Bit" : "16 Bit");

                    const char* str;
                    switch(mode)
                    {
                    case DEBUGGER_DMAMODE_AGB:
                    {
                        if (i == 3)
                        {
                            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                                ImGui_Text("Game Pak DRQ: %s", dma[i].CR.AGBPakIRQ ? "32 Bit" : "16 Bit");
                        }
                        switch(dma[i].CR.StartMode7)
                        {
                        case 0: str = "Immediate"; break;
                        case 1: str = "VBlank"; break;
                        case 2: str = "HBlank"; break;
                        case 3: 
                            switch(i)
                            {
                            case 0: str = "Prohibited"; break;
                            case 1 ... 2: str = "Sound FIFO"; break;
                            case 3: str = "Video"; break;
                            }
                            break;
                        }
                        break;
                    }
                    case DEBUGGER_DMAMODE_NTR7:
                    {
                        switch(dma[i].CR.StartMode7)
                        {
                        case 0: str = "Immediate"; break;
                        case 1: str = "VBlank"; break;
                        case 2: str = "Game Card Transfer"; break;
                        case 3: 
                            switch(i)
                            {
                            case 0 ... 1: str = "WiFi IRQ?"; break;
                            case 2 ... 3: str = "Game Pak DRQ"; break;
                            }
                            break;
                        }
                        break;
                    }
                    case DEBUGGER_DMAMODE_NTR9:
                    {
                        switch(dma[i].CR.StartMode9)
                        {
                        case 0: str = "Immediate"; break;
                        case 1: str = "VBlank"; break;
                        case 2: str = "HBlank"; break;
                        case 3: str = "Video"; break;
                        case 4: str = "Display FIFO"; break;
                        case 5: str = "Game Card Transfer"; break;
                        case 6: str = "Game Pak DRQ"; break;
                        case 7: str = "GX Command FIFO"; break;
                        }
                        break;
                    }
                    }
                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        ImGui_TextUnformatted(str);

                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        ImGui_Text("IRQ: %s", dma[i].CR.IRQ ? "Enabled" : "Disabled");

                    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                        ImGui_Text("Enable: %s", dma[i].CR.IRQ ? "Enabled" : "Disabled");

                    ImGui_TreePop();
                }

                ImGui_TreePop();
            }
        }

        ImGui_TreePop();
    }
}

void DebugGui_IO9(DebugGui* dgui, Console* sys)
{
    if (dgui->IO9 && ImGui_Begin("IO9 Viewer", &dgui->IO9, 0))
    {
        if (sys && ImGui_BeginTable("IO9 Debugger", 2, ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders))
        {
            ImGui_TableNextRow();
            ImGui_TableNextColumn();
            if (ImGui_TreeNode("PPU A"))
            {
                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                if (ImGui_TreeNode("DispCnt"))
                {
                    ImGui_TableNextRow();
                    ImGui_TableNextColumn();
                    ImGui_Text("0:2");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background Mode: %i", sys->PPU_A.DisplayCR.BGMode);
                    ImGui_TableNextColumn();
                    ImGui_Text("3");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background 0 3D/2D: %s", sys->PPU_A.DisplayCR.BG03D ? "3D" : "2D");
                    ImGui_TableNextColumn();
                    ImGui_Text("4");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Tile Mapping: %s", sys->PPU_A.DisplayCR.TileOBJ1D ? "1D" : "2D");
                    ImGui_TableNextColumn();
                    ImGui_Text("5");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Bitmap 2D Dim: %s", sys->PPU_A.DisplayCR.BitmapOBJ2DDims ? "128x512" : "256x256");
                    ImGui_TableNextColumn();
                    ImGui_Text("6");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Bitmap Mapping: %s", sys->PPU_A.DisplayCR.BitmapOBJ1D ? "1D" : "2D");
                    ImGui_TableNextColumn();
                    ImGui_Text("7");
                    ImGui_TableNextColumn();
                    ImGui_Text("Force Blank: %s", sys->PPU_A.DisplayCR.ForceBlank ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("8");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background 0 %s", sys->PPU_A.DisplayCR.BG0Enable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("9");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background 1: %s", sys->PPU_A.DisplayCR.BG1Enable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("10");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background 2: %s", sys->PPU_A.DisplayCR.BG2Enable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("11");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background 3: %s", sys->PPU_A.DisplayCR.BG3Enable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("12");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprites: %s", sys->PPU_A.DisplayCR.SprEnable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("13");
                    ImGui_TableNextColumn();
                    ImGui_Text("Window 0: %s", sys->PPU_A.DisplayCR.Win0Enable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("14");
                    ImGui_TableNextColumn();
                    ImGui_Text("Window 1: %s", sys->PPU_A.DisplayCR.Win1Enable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("15");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Window: %s", sys->PPU_A.DisplayCR.OBJWinEnable ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("16:17");
                    ImGui_TableNextColumn();
                    constexpr char LCDModes[4][20] = {"Display Off", "PPU Output", "VRAM Bitmap Display", "FIFO Bitmap Display"};
                    ImGui_Text("LCD Mode: %s", LCDModes[sys->PPU_A.DisplayCR.DisplayMode]);
                    ImGui_TableNextColumn();
                    ImGui_Text("18:19");
                    ImGui_TableNextColumn();
                    ImGui_Text("VRAM Bank Select: %c", 'A'+sys->PPU_A.DisplayCR.VRAMSel);
                    ImGui_TableNextColumn();
                    ImGui_Text("20:21");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Tile 1D-Boundary: %i", 32<<sys->PPU_A.DisplayCR.TileOBJ1DBound);
                    ImGui_TableNextColumn();
                    ImGui_Text("22");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Bitmap 1D-Boundary: %i", 128<<sys->PPU_A.DisplayCR.BitmapOBJ1DBound);
                    ImGui_TableNextColumn();
                    ImGui_Text("23");
                    ImGui_TableNextColumn();
                    ImGui_Text("HBlank Sprite Processing: %s", sys->PPU_A.DisplayCR.OBJHBlankDisable ? "Disable" : "Enable");
                    ImGui_TableNextColumn();
                    ImGui_Text("24:26");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background Char Base: %iK", 64*sys->PPU_A.DisplayCR.CharBase);
                    ImGui_TableNextColumn();
                    ImGui_Text("27:29");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background Screen Base: %iK", 64*sys->PPU_A.DisplayCR.ScreenBase);
                    ImGui_TableNextColumn();
                    ImGui_Text("30");
                    ImGui_TableNextColumn();
                    ImGui_Text("Background Extended Palette: %s", sys->PPU_A.DisplayCR.BGExtPalEn ? "Enabled" : "Disabled");
                    ImGui_TableNextColumn();
                    ImGui_Text("31");
                    ImGui_TableNextColumn();
                    ImGui_Text("Sprite Extended Palette: %s", sys->PPU_A.DisplayCR.SprExtPalEn ? "Enabled" : "Disabled");
                    ImGui_TreePop();
                }
                ImGui_TreePop();
            }
            ImGui_EndTable();
        }
        ImGui_End();
    }
}

void DebugGui_PSGEnvelope(CGBSndEnvelope* env, CGBSndEnvelopeLatch* lat, const char* const name)
{
    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
    if (ImGui_TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
    {
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("Pace: %u", env->Pace);
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("Direction: %s", env->Inc ? "Increasing" : "Decreasing");
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("Start Volume: %u", env->StartVol);

        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("Cur Timer: %u", lat->Tick);
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("Cur Volume: %u", lat->Vol);
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("DAC Enable: %u", env->DACEnable != 0);

        ImGui_TreePop();
    }
}

void DebugGui_PSGSquare(Console* sys, u8 id)
{
    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
    if (ImGui_TreeNodeEx((id==0) ? "PSG 1" : "PSG 2", ImGuiTreeNodeFlags_DrawLinesFull))
    {
        if (id==0)
        {
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            if (ImGui_TreeNodeEx("Sweep (NR10)", ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
            {
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Shift: %u", sys->CGBPulseChannel[id].Regs.Sweep.Shift);
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Direction: %s", sys->CGBPulseChannel[id].Regs.Sweep.Dec ? "Decreasing" : "Increasing");
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Pace: %u", sys->CGBPulseChannel[id].Regs.Sweep.Pace);

                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Cur Timer: %u", sys->CGBPulseChannel[id].Latched.Sweep.Tick);
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Latched Frequency: %u", sys->CGBPulseChannel[id].Latched.Sweep.Freq);
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                    ImGui_Text("Enable: %s", sys->CGBPulseChannel[id].Latched.Sweep.On ? "Enabled" : "Disabled");


                ImGui_TreePop();
            }
        }

        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
        if (ImGui_TreeNodeEx((id==0) ? "Length + Duty (NR11)" : "Length + Duty (NR21)", ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
        {
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Length: %u", sys->CGBPulseChannel[id].Regs.Length);
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                constexpr char DutyNames[][4] = {"1/8", "1/4", "1/2", "3/4"};
                ImGui_Text("Duty: %u (%s)", sys->CGBPulseChannel[id].Regs.DutyPattern, DutyNames[sys->CGBPulseChannel[id].Regs.DutyPattern]);

            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Cur Length Timer: %u", sys->CGBPulseChannel[id].Latched.LenTick);
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Cur Duty Tick: %u", sys->CGBPulseChannel[id].Latched.DutyTick);

            ImGui_TreePop();
        }

        DebugGui_PSGEnvelope(&sys->CGBPulseChannel[id].Regs.Envelope, &sys->CGBPulseChannel[id].Latched.Envelope, (id==0) ? "Envelope (NR12)" : "Envelope (NR22)");

        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
        if (ImGui_TreeNodeEx((id==0) ? "Control (NR13 & NR14)" : "Control (NR23 & NR24)", ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
        {
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Frequency: %u", sys->CGBPulseChannel[id].Regs.Freq);
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Use Length: %s", sys->CGBPulseChannel[id].Regs.UseLength ? "Enabled" : "Disabled");

            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Running: %s", ((id==0) ? sys->AGBSndEn.Psg1Running : sys->AGBSndEn.Psg2Running) ? "Enabled" : "Disabled");
            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("Cur Sample: %"PRIi8"", sys->AGBSamples[id]);

            ImGui_TreePop();
        }

        ImGui_TreePop();
    }
}

void DebugGui_PCMAudio(Console* sys, u8 id)
{
    ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
    if (ImGui_TreeNodeEx((id==0) ? "PCM A" : "PCM B", ImGuiTreeNodeFlags_DrawLinesFull))
    {
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("FIFO Fill Pointer:  %u", sys->AGBSndFIFOFillPtr[id]);
        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("FIFO Drain Pointer: %u", sys->AGBSndFIFODrainPtr[id]);

        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
        if (ImGui_TreeNodeEx("FIFO Data", ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
        {
            for (int i = 0; i < 8; i++)
            {
                ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
                ImGui_Text("%08X", sys->AGBSndFIFO[id].b32[i]);
            }

            ImGui_TreePop();
        }

        ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            ImGui_Text("Cur Sample: %u", sys->AGBSamples[id+4]);

        ImGui_TreePop();
    }
}

void DebugGui_IOAGB(DebugGui* dgui, Console* sys)
{
    if (dgui->IOAGB && ImGui_Begin("IOAGB Viewer", &dgui->IOAGB, 0))
    {
        if (sys && ImGui_BeginTable("IOAGB Debugger", 1, ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_BordersOuter))
        {
            DebugGui_DMA(&sys->DMA7[DMA7_NormalBase], "DMA", DEBUGGER_DMAMODE_AGB);

            ImGui_TableNextRow(); ImGui_TableSetColumnIndex(0);
            if (ImGui_TreeNodeEx("Sound Channels", ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_DrawLinesFull))
            {
                DebugGui_PSGSquare(sys, 0);
                DebugGui_PSGSquare(sys, 1);

                DebugGui_PCMAudio(sys, 0);
                DebugGui_PCMAudio(sys, 1);

                ImGui_TreePop();
            }

            ImGui_EndTable();
        }

        ImGui_End();
    }
}

void DebugGUI_Loop(MailBox* mail, MainGUI* mgui, Console* sys)
{
    if (mgui->dbg.A9DbgDisplay) DebugGui_A9(mail, &mgui->dbg, sys);
    if (mgui->dbg.A7DbgDisplay) DebugGui_A7(mail, &mgui->dbg, sys);
    if (mgui->dbg.Sched) DebugGui_Sched(&mgui->dbg, sys);
    DebugGui_IO9(&mgui->dbg, sys);
    DebugGui_IOAGB(&mgui->dbg, sys);
}
