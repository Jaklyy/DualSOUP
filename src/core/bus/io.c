#include "../utils.h"
#include "../dma/dma.h"
#include "../console.h"




void Bus7_IO_Write(struct Console* sys, u32 address, u32 val, const u32 mask)
{
    switch(address & 0xFF'FF'FF)
    {
        // 2D GPU
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