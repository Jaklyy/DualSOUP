#pragma once
#include "../utils.h"
#include "../console.h"




enum IRQs
{
    // AGB irqs
    IRQ_VBlank,
    IRQ_HBlank,
    IRQ_VCount,
    IRQ_Timer0,
    IRQ_Timer1,
    IRQ_Timer2,
    IRQ_Timer3,
    IRQ_SerialIO, // arm7
    IRQ_DMA0,
    IRQ_DMA1,
    IRQ_DMA2,
    IRQ_DMA3,
    IRQ_Keypad,
    IRQ_AGBPak,

    IRQ_Unused1,
    IRQ_Unused2,

    // NTR irqs
    IRQ_IPCSync,
    IRQ_IPCFIFOEmpty,
    IRQ_IPCFIFONotEmpty,
    IRQ_GamecardTransferComplete,
    IRQ_GamecardIRQMC, // what?
    IRQ_3DFIFO, // arm9
    IRQ_LidOpen, // arm7
    IRQ_SPI, // arm7
    IRQ_WiFi, // arm7

    IRQ_Max
};

u32 IO7_Read(struct Console* sys, const u32 addr, const u32 mask);
void IO7_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask);
u32 IO9_Read(struct Console* sys, const u32 addr, const u32 mask);
void IO9_Write(struct Console* sys, const u32 addr, const u32 val, const u32 mask);
