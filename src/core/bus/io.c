#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"




void Bus7_IO_Write(struct Console* sys, u32 address, u32 val, const u32 mask)
{
    switch(address & 0xFF'FF'FC)
    {
        // 2D GPU / LCD
        case 0x00'00'04: // dispstat
        case 0x00'00'05:
            break;
        case 0x00'00'06: // vcount
        case 0x00'00'07: // vcount
            break; 

        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1:
            DMA7_IOWriteHandler(sys->DMA7, address, val, mask);
            break;
    }
}

u32 Bus9_IO_Read(struct Console* sys, const u32 address, const u32 mask)
{
    switch (address & 0xFF'FF'FC)
    {
        // LCD A + 2D A; and a lil 3D for w/e reason
        case 0x00'00'00: // display control A
        case 0x00'00'04: // display status & vertical count
        case 0x00'00'08 ... 0x00'00'60-1: // 2D A
        case 0x00'00'60: // 3d control
        case 0x00'00'64: // display capture control
        case 0x00'00'68: // display fifo
        case 0x00'00'6C: // Master Brightness A

        // DMA
        case 0x00'00'B0 ... 0x00'00'E0-1: // dma control
        case 0x00'00'E0 ... 0x00'00'F0-1: // dma fill

        // Timers
        case 0x00'01'00 ... 0x00'01'10-1:

        // Half of button input for some reason
        case 0x00'01'30:

        // IPC
        case 0x00'01'80: // IPCSync
        case 0x00'01'84: // IPC FIFO Control
        case 0x00'01'88: // IPC FIFO Send

        // Gamecard
        case 0x00'01'A0: // SPI
        case 0x00'01'A4: // ROM Control
        case 0x00'01'A8 ... 0x00'01'B0-1: // ROM Command Out
        case 0x00'01'B0 ... 0x00'01'BC-1: // Encryption junk

        // IRQ and External Memory
        case 0x00'02'04: // External Memory Control
        case 0x00'02'08: // IME
        case 0x00'02'10: // IE
        case 0x00'02'14: // IF

        // Internal Memory
        case 0x00'02'40 ... 0x00'02'50-1:

        // Math Coprocessors
        case 0x00'02'80: // Division Control
        case 0x00'02'90 ... 0x00'02'98-1: // Division Numerator
        case 0x00'02'98 ... 0x00'02'A0-1: // Division Denominator
        case 0x00'02'A0 ... 0x00'02'A8-1: // Division Quotient
        case 0x00'02'A8 ... 0x00'02'B0-1: // Division Remainder
        case 0x00'02'B0: // Square Root Control
        case 0x00'02'B4: // Square Root Result
        case 0x00'02'B8 ... 0x00'02'C0-1: // Square Root Input

        // misc
        case 0x00'03'00: // Post Flag
        case 0x00'03'04: // Graphics Power Control (and Display Swap for some reason?)

        // 3d Graphics
        case 0x00'03'20 ... 0x00'06'A4-1:

        // LCD B + 2D B
        case 0x00'10'00 ... 0x00'10'70-1:

        // TWL regs

        // IPC FIFO Recieve
        case 0x10'00'00:

        // Gamecard Recieve (but also out??)
        case 0x10'00'10:
            break;
        default:
            break;
    }


}
