#include "core/utils.h"
#include "core/console.h"
#include "bus.h"
#include "core/scheduler.h"



void WiFi_Init(Console* sys)
{
    sys->WiFiBB[0x00] = 0x6D;
    sys->WiFiBB[0x5D] = 0x01;
    sys->WiFiBB[0x64] = 0xFF;
}

void WiFi_Read(Console* sys, u32* rdata, timestamp* now, const u32 addr, const AHB_HSIZE size)
{
    if (!sys->PowerCR7.WifiPower // checkme: does this return 0?
        || (addr >= 0x04810000)) // melonds does this deliberately so im gonna assume this is how this works?
    {
        *now += DSClk33(1); // checkme
        *rdata = 0;
        return;
    }

    *now += DSClk33(1); // TODO

    switch (addr & 0x6000)
    {
        case 0x0000: break; // io?
        case 0x2000: *rdata = 0xFFFFFFFF; return; // idk lets just blindly trust melonds here
        case 0x4000: *rdata = MemoryRead(32, sys->WiFiRAM, addr, WiFiRAM_Size); return; // ram
        case 0x6000: break; // ???
        default: unreachable();
    }

    // io junk
    switch (addr & 0xFFC)
    {
        case 0x034:
            *rdata = sys->WiFiPowerUS.Raw << 16;
            break;
        case 0x03C:
            *rdata = 0x0200;
            break;
        case 0x158:
            *rdata = 0;
            break;
        case 0x15C:
            *rdata = (sys->WiFiPowerUS.PowerOff ? 0 : sys->WiFiBBRdBuf);
            break;
        case 0x180:
            *rdata = 0;
            break;
        case 0x214:
            *rdata = 9;
            break;
        default:
            *rdata = MemoryRead(32, sys->WifiIO, addr, 0x1000); // TODO
            LogPrint(LOG_UNIMP|LOG_WIFI, "NTR Bus7: Unimplemented READ%"PRIu32": WiFi IO %08"PRIX32" %08"PRIX32"\n", (8<<size), addr, *rdata);
            break;
    }
}

void WiFi_Write(Console* sys, timestamp* now, const u32 addr, const u32 wrdata, const u32 mask)
{
    const u32 width = stdc_count_ones(mask);

    if (!sys->PowerCR7.WifiPower
        || (addr >= 0x04810000) // melonds does this deliberately so im gonna assume this is how this works?
        || (width == 8)) // checkme: gbatek claims 8 bit wide writes dont work for the wifi region?
    {
        // CHECKME: contention for bytes?
        *now += DSClk33(1); // TODO // checkme?
        return;
    }

    *now += DSClk33(1); // TODO

    switch (addr & 0x6000)
    {
        case 0x0000: // io?
            break;
        case 0x2000: // idk lets just blindly trust melonds here
            return;
        case 0x4000: // ram
            MemoryWrite(32, sys->WiFiRAM, addr, WiFiRAM_Size, wrdata, mask);
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
                MaskedWrite(sys->WiFiPowerUS.Raw, wrdata >> 16, 0x3);
            }
            break;

        case 0x158:
            if (sys->WiFiPowerUS.PowerOff) break; // checkme
            if (mask & 0x0000FFFF)
            {
                u8 idx = (wrdata & 0xFF);
                if ((wrdata >> 12) == 5)
                {
                    bool pass;
                    if (idx < 0x40)
                    {
                        pass = (!(((u64)1 << idx) & 0x0000'0080'07C7'E001));
                    } 
                    else if (idx < 0x69)
                    {
                        pass = (!(((u64)1 << (idx-0x40)) & 0xFFFF'FE52'E000'2000));
                    }
                    else pass = false;

                    if (pass)
                    {
                        sys->WiFiBB[idx] = sys->WiFiBBWrBuf;
                    }
                    else
                    {
                        LogPrint(LOG_WIFI, "Invalid ");
                    }
                    LogPrint(LOG_WIFI, "BB WR: %02"PRIX8" %02"PRIX8"\n", idx, sys->WiFiBBWrBuf);
                }
                else if ((wrdata >> 12) == 6)
                {
                    sys->WiFiBBRdBuf = sys->WiFiBB[idx];
                    LogPrint(LOG_WIFI, "BB RD: %02"PRIX8" %02"PRIX8"\n", idx, sys->WiFiBBRdBuf);
                }
            }
            if (mask & 0xFFFF0000)
            {
                sys->WiFiBBWrBuf = (wrdata >> 16) & 0xFF;
            }
            break;

        default:
            LogPrint(LOG_UNIMP|LOG_WIFI, "NTR Bus7: Unimplemented WRITE%"PRIu32": WiFi IO %08"PRIX32" %08"PRIX32"\n", width, addr, wrdata);
            MemoryWrite(32, sys->WifiIO, addr, 0x1000, wrdata, mask); // TODO
            break;
    }
}
