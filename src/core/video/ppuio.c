#include "core/utils.h"
#include "ppu.h"




u32 PPU_IORead(PPU* ppu, const u32 addr)
{
    switch(addr & 0x7C)
    {
    case 0x00: // ppu + lcdc
        return ppu->DisplayCR.Raw;

    case 0x08:
        return ppu->BGCR[0].Raw | (ppu->BGCR[1].Raw << 16);
    case 0x0C:
        return ppu->BGCR[2].Raw | (ppu->BGCR[3].Raw << 16);

    case 0x48:
        return ppu->Window.Raw[2];

    case 0x50:
        return ppu->BlendCR.Raw | ppu->BlendAlpha[0] << 16 | ppu->BlendAlpha[1] << 24;

    case 0x6C: // lcdc
        return ppu->Brightness.Raw;

    default:
        LogPrint(LOG_PPU|LOG_UNIMP|LOG_IO, "Unimplemented PPU Read: %08"PRIX32"\n", addr);
        return 0;
    }
}

void PPU_IOWrite(PPU* ppu, const u32 addr, const u32 val, u32 mask, const bool b, const bool ppuenable)
{
    if (!ppuenable && ((addr & 0x7F) >= 0x8) && ((addr & 0x7F) < 0x60))
    {
        LogPrint(LOG_PPU|LOG_ODD|LOG_IO, "Writing PPU Regs while PPU disabled? %08"PRIX32" %08"PRIX32" %08"PRIX32"\n", addr, val, mask);
        return;
    }
    switch(addr & 0x7F)
    {
    case 0x00: // ppu and lcdc control reg, so it isn't disabled with the ppu
        MaskedWrite(ppu->DisplayCR.Raw, val, mask & DispCRWrMasks[b]);
        break;

    case 0x08:
        MaskedWrite(ppu->BGCR[0].Raw, val, mask);
        MaskedWrite(ppu->BGCR[1].Raw, val>>16, mask>>16);
        break;
    case 0x0C:
        MaskedWrite(ppu->BGCR[2].Raw, val, mask);
        MaskedWrite(ppu->BGCR[3].Raw, val>>16, mask>>16);
        break;

    case 0x10:
        MaskedWrite(ppu->Xoff[0], val, mask & BGOffsetWrMask);
        MaskedWrite(ppu->Yoff[0], val>>16, (mask>>16) & BGOffsetWrMask);
        break;
    case 0x14:
        MaskedWrite(ppu->Xoff[1], val, mask & BGOffsetWrMask);
        MaskedWrite(ppu->Yoff[1], val>>16, (mask>>16) & BGOffsetWrMask);
        break;
    case 0x18:
        MaskedWrite(ppu->Xoff[2], val, mask & BGOffsetWrMask);
        MaskedWrite(ppu->Yoff[2], val>>16, (mask>>16) & BGOffsetWrMask);
        break;
    case 0x1C:
        MaskedWrite(ppu->Xoff[3], val, mask & BGOffsetWrMask);
        MaskedWrite(ppu->Yoff[3], val>>16, (mask>>16) & BGOffsetWrMask);
        break;

    case 0x40 ... 0x48:
    {
        u8 idx = (addr/4) % 4;
        MaskedWrite(ppu->Window.Raw[idx], val, mask & WinCRWrMasks[idx]);
        break;
    }

    case 0x50:
        MaskedWrite(ppu->BlendCR.Raw, val, mask & BlendCRWrMask);
        MaskedWrite(ppu->BlendAlpha[0], val>>16, (mask>>16) & BlendParamWrMask);
        MaskedWrite(ppu->BlendAlpha[1], val>>24, (mask>>24) & BlendParamWrMask);
        break;
    case 0x54:
        MaskedWrite(ppu->BlendBright, val, mask & BlendParamWrMask);
        break;

    case 0x6C: // lcdc, not ppu; not disabled by ppu control
        MaskedWrite(ppu->Brightness.Raw, val, mask & LCDCBrightnessWrMask);
        break;

    default:
        LogPrint(LOG_PPU|LOG_UNIMP|LOG_IO, "Unimplemented PPU write: %08"PRIX32" %08"PRIX32" %08"PRIX32"\n", addr, val, mask);
        break;
    }
}
