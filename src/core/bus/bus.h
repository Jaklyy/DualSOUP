#pragma once

#include "../utils.h"




enum NTRBus_Devices : u8
{
    Dev_Abort, // sets a flag to raise an abort in the future
    Dev_ITCM,
    Dev_DTCM, // only on data buses

    Dev_Dummy, // explicitly mapped as unmapped memory
    Dev_BIOS,
    Dev_MainRAM,
    Dev_A7WRAM, // ARM7 only
    Dev_SWRAM_Lo,
    Dev_SWRAM_Hi,
    Dev_IO,
    Dev_Wifi0,
    Dev_Wifi1,
    Dev_GBAROM,
    Dev_GBAROM_Unmap, // always 0
    Dev_GBARAM,
    Dev_GBARAM_Unmap, // always 0

    // ya like vram?
    Dev_LCD_A,
    Dev_LCD_B,
    Dev_LCD_C,
    Dev_LCD_D,
    Dev_LCD_E,
    Dev_LCD_F,
    Dev_LCD_G,
    Dev_LCD_H,
    Dev_LCD_I,

    Dev_BG0_A,
    Dev_BG0_B,
    Dev_BG0_C,
    Dev_BG0_D,
    Dev_BG0_E,
    Dev_BG0_F,
    Dev_BG0_G,
    Dev_BG0_Slow,

    Dev_OBJ0_A,
    Dev_OBJ0_B,
    Dev_OBJ0_E,
    Dev_OBJ0_F,
    Dev_OBJ0_G,
    Dev_OBJ0_Slow,

    Dev_BG1_C,
    Dev_BG1_H,
    Dev_BG1_I,
    Dev_BG1_Slow,

    Dev_OBJ1_D,
    Dev_OBJ1_I,
    Dev_OBJ1_Slow,

    Dev_A7_C,
    Dev_A7_D,
    Dev_A7_Slow,

    Dev_BGPal0,
    Dev_BGPal1,
    Dev_OBJPal0,
    Dev_OBJPal1,
    Dev_OAM0,
    Dev_OAM1,

    // woe, New Shared WRAM upon ye.
    Dev_NSWRAM_A0,
    Dev_NSWRAM_A1,
    Dev_NSWRAM_A2,
    Dev_NSWRAM_A3,
    Dev_NSWRAM_B0,
    Dev_NSWRAM_B1,
    Dev_NSWRAM_B2,
    Dev_NSWRAM_B3,
    Dev_NSWRAM_B4,
    Dev_NSWRAM_B5,
    Dev_NSWRAM_B6,
    Dev_NSWRAM_B7,
    Dev_NSWRAM_C0,
    Dev_NSWRAM_C1,
    Dev_NSWRAM_C2,
    Dev_NSWRAM_C3,
    Dev_NSWRAM_C4,
    Dev_NSWRAM_C5,
    Dev_NSWRAM_C6,
    Dev_NSWRAM_C7,

    Dev_MainRAM_OpenBus, // Retail TWL only

    // extra expansion room, dont adjust anything in this enum past here

    // not actually devices; for internal emulator use only.
    Dev_Wait, // access must be deferred til later.
    Dev_None, // no device.

    Dev_Cache = 0x80, // should do a cached check beforehand
};

static_assert(Dev_None < 0x80); // i swear if i somehow wind up needing 128 regions

enum ARM9_BusControl : u8
{
    PRIO_ODMA, // idk where the term "ODMA" originates from but I have idea how else to refer to AGB/NTR style DMA vs TWL/CTR style.
    PRIO_ARM9,
};

enum BusReturn : u8
{
    BUSRET_ERROR,
    BUSRET_WAIT,
    BUSRET_PASS,
};

// Can also be used to enforce appropriate alignment.
enum BusAccessWidth : u8
{
    BusAccess_8Bit = 0,
    BusAccess_16Bit = 1,
    BusAccess_32Bit = 3,
};

struct Bus
{
    u64 Timestamp; // Bus Timestamp.
    u64 BusyDeviceTS; // when the currently busy device will stop being busy.
    u32 CurOpenBus; // TODO: is this needed?
    u8 BusyDevice; // The most recently accessed device.
    u8 CurRequests; // List of requests for bus ownership; This will need to be expanded for DSi/3DS stuff.
    u8 CurOwner; // Who is currently owner of the bus.
};

struct BusTiming
{
    u8 NTiming; // Nonsequential
    u8 STiming; // Sequential
};

struct BusRet
{
    u32 Val;
    u8 Cycles;
    u8 Device;
    // struct BusTiming Timings;
    //u8 ErrorCode;
};

// MainRAM is a type of FCRAM.
// gbatek lists the following chips as being used in retail DS models:
// Fujitsu 82DBS02163C-70L
// ST Microelectronics M69AB048BL70ZA8
// MainRAM only natively supports 16 bit accesses.
// not 100% clear what mechanic the nds uses to support 8 bit writes?
// 8bit reads presumably use some special logic or rely on the fact that our specific ARM processors
// seem to always perform a rotate right and bit masking operation on byte reads
struct BusMainRAM
{
    timestamp Timestamp; // 
    // pointers to timestamps for each bus that can access main ram.
    // if main ram is not in danger then that side should point to when they next awaken
    // if main ram is in active danger then this should point to that side's Bus Timestamp.

    /*
    Danger Timestamp should be:

    when all components asleep:
        expected wake up time

    when one component is awake:


    ARM9 is comprised of:
    ARM9InstrTime
    ARM9DataTime
    ARM9WriteTime
    Bus9Timestamp
    */

    timestamp* ARM9ThreatTimestamp;
    timestamp* ARM7ThreatTimestamp;
    // Internal control reg for the FCRAM chip on the NDS.
    // NTR/USG ARM9 BIOS has init code for mainRAM @ offset 0x180.
    // Should be initialized using halfword r/w to the most significant halfword of mainRAM.
    // CR set sequence for Fujitsu MB82DBS02163C-70L:
    // Read data -> write the data back twice -> 2 more writes of any value -> read from any mainram address (address is written to the control reg).
    // BIOS init code goes out of its way to use specific values for the last two writes for some reason. Might be used for other chips?
    // MainRAM is addressed in halfwords so the LSB of the address is ignored(?).
    // Not sure how this works with byte accesses (most likely depends on what jank they're doing to fake byte support).
    // Word accesses shouldn't dont work though.
    // TODO: implement main ram init process?
    // note: the data sheet seems to be wrong? timings used seem to somehow be: 5 cycles read; 3 cycles write. Which isn't possible for this ram chip allegedly?
    union
    {
        u32 Raw;
        struct
        {
            // "must be one"
            u32 : 7;
            // 0 = single clock pulse control, no write suspend;
            // 1 = level control, write suspend;
            bool WriteControl : 1;
            // "must be one"
            u32 : 1;
            // 0 = falling edge;
            // 1 = rising edge;
            bool ValidClockEdge : 1;
            // disables burst write support when set
            bool SingleWrite : 1;
            // 0 = reserved;
            // 1 = sequential;
            bool BurstSequence : 1;
            // 1-3 = 4-6 cycles (read) & 3-5 cycles (write);
            // other values are reserved;
            u32 Latency : 3;
            // 0 = synchronous;
            // 1 = asynchronous;
            bool Mode : 1;
            // 2 = 8 word;
            // 3 = 16 word;
            // 7 = continuous; (seems to have a 241 cycle limit?)
            // other values are reserved;
            u32 BurstLength : 3;
            // power saving feature?
            // 0 = 8 M-bit partial;
            // 1 = 4 M-bit partial;
            // 2 = reserved;
            // 3 = Sleep;
            u32 PartialSize : 2;
        };
    } ControlReg;
};

void Bus_MainRAM();
void Bus9();
