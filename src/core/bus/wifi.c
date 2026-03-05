#include <stdbit.h>
#include "../utils.h"
#include "../console.h"
#include "ahb.h"



void WiFi_Init(struct Console* sys)
{
    sys->WiFiBB[0x00] = 0x6D;
    sys->WiFiBB[0x5D] = 0x01;
    sys->WiFiBB[0x64] = 0xFF;
}

u32 WiFi_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, bool* seq, const bool timings)
{
    const unsigned width = stdc_count_ones(mask);
    u32 ret;

    if (!sys->PowerCR7.WifiPower // checkme: does this return 0?
        || (addr >= 0x04810000)) // melonds does this deliberately so im gonna assume this is how this works?
    {
        if (timings) Timing32(&sys->AHB9); // checkme
        return 0;
    }

    if (timings) Timing32(&sys->AHB7); // TODO

    switch (addr & 0x6000)
    {
        case 0x0000: // io?
            break;
        case 0x2000: // idk lets just blindly trust melonds here
            return 0xFFFFFFFF;
        case 0x4000: // ram
            return MemoryRead(32, sys->WiFiRAM, addr, WiFiRAM_Size);
        case 0x6000: // ???
            break;
        default: unreachable();
    }

    // io junk
    switch (addr & 0xFFC)
    {
        case 0x034:
            ret = sys->WiFiPowerUS.Raw << 16;
            break;
        case 0x03C:
            ret = 0x0200;
            break;
        case 0x158:
            ret = 0;
            break;
        case 0x15C:
            ret = (sys->WiFiPowerUS.PowerOff ? 0 : sys->WiFiBBRdBuf);
            break;
        case 0x180:
            ret = 0;
            break;
        case 0x214:
            ret = 9;
            break;
        default:
            ret = MemoryRead(32, sys->WifiIO, addr, 0x1000); // TODO
            break;
    }
    if (timings) LogPrint(LOG_UNIMP|LOG_WIFI, "NTR_AHB7: Unimplemented READ%i: WiFi IO %08X %08X %08X\n", width, addr, ret, mask);
    return ret;
}

void WiFi_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, bool* seq, const bool timings)
{
    const unsigned width = stdc_count_ones(mask);

    if (!sys->PowerCR7.WifiPower
        || (addr >= 0x04810000) // melonds does this deliberately so im gonna assume this is how this works?
        || (width == 8)) // checkme: gbatek claims 8 bit wide writes dont work for the wifi region?
    {
        if (timings)
        {
            // CHECKME: contention for bytes?
            Timing32(&sys->AHB9); // checkme?
        }
        return;
    }

    if (timings) Timing32(&sys->AHB7); // TODO

    switch (addr & 0x6000)
    {
        case 0x0000: // io?
            break;
        case 0x2000: // idk lets just blindly trust melonds here
            return;
        case 0x4000: // ram
            MemoryWrite(32, sys->WiFiRAM, addr, WiFiRAM_Size, val, mask);
            return;
        case 0x6000: // ???
            break;
        default: unreachable();
    }

    // io junk
    switch (addr & 0xFFC)
    {
        case 0x034:
            if (mask & 0xFFFF0000)
            {
                MaskedWrite(sys->WiFiPowerUS.Raw, val >> 16, 0x3);
            }
            break;

        case 0x158:
            if (sys->WiFiPowerUS.PowerOff) break; // checkme
            if (mask & 0x0000FFFF)
            {
                u8 idx = (val & 0xFF);
                if ((val >> 12) == 5)
                {
                    if (((idx < 0x40) && !((1ull << idx) & 0x0000'0080'07C7'E001))
                        || ((idx < 0x69) && !((1ull << (idx-0x40)) & 0xFFFF'FE52'E000'2000)))
                    {
                        sys->WiFiBB[idx] = sys->WiFiBBWrBuf;
                    }
                    else
                    {
                        LogPrint(LOG_WIFI, "INV ");
                    }
                    LogPrint(LOG_WIFI, "BB WR: %02X %02X\n", idx, sys->WiFiBBWrBuf);
                }
                else if ((val >> 12) == 6)
                {
                    sys->WiFiBBRdBuf = sys->WiFiBB[idx];
                    LogPrint(LOG_WIFI, "BB RD: %02X %02X\n", idx, sys->WiFiBBRdBuf);
                }
            }
            if (mask & 0xFFFF0000)
            {
                sys->WiFiBBWrBuf = (val >> 16) & 0xFF;
            }
            break;

        default:
            MemoryWrite(32, sys->WifiIO, addr, 0x1000, val, mask); // TODO
            break;
    }
    if (timings) LogPrint(LOG_UNIMP|LOG_WIFI, "NTR_AHB7: Unimplemented WRITE%i: WiFi IO %08X %08X %08X\n", width, addr, val, mask);
}
