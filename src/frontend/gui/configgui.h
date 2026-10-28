#pragma once

#include "maingui.h"
#include "frontend/soupparser/soupparser.h"



typedef struct
{

} ConfigGUI;

void ConfigGUI_Loop(MainGUI* mgui, MainCfg* mcfg);

void NDSImportGui_Toggle(MainGUI* mgui, const char* editfolder);
void NDSImportGui_Loop(MainGUI* mgui, MainCfg* mcfg);

void SysImportGui_Toggle(MainGUI* mgui, const char* editfolder);
void SysImportGui_Loop(MainGUI* mgui, MainCfg* mcfg);

void GBAImportGui_Toggle(MainGUI* mgui, const char* editfolder);
void GBAImportGui_Loop(MainGUI* mgui, MainCfg* mcfg);
