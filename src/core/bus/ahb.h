#pragma once
#include "../utils.h"


struct Console;

// used for modelling write contention
enum NTRAHB_Devices : u8
{
    Dev_Bios7, // ARM7 only
    Dev_MainRAM,
    Dev_WRAM,
    Dev_A7WRAM, // ARM7 only
    Dev_IO,

    // ya like vram?
    Dev_VRAM_A,
    Dev_VRAM_B,
    Dev_VRAM_C,
    Dev_VRAM_D,
    Dev_VRAM_E,
    Dev_VRAM_F,
    Dev_VRAM_G,
    Dev_VRAM_H,
    Dev_VRAM_I,

    Dev_Palette,
    Dev_OAM,

    Dev_MAX,
};

enum ARM9_AHBPriorities : u8
{
    AHB9Prio_DMABase, // 0 == highest priority
    AHB9Prio_DMA0 = AHB9Prio_DMABase,
    AHB9Prio_DMA1,
    AHB9Prio_DMA2,
    AHB9Prio_DMA3,
    AHB9Prio_ARM9,
};

struct AHB
{
    timestamp Timestamp; // AHB Timestamp.
    timestamp BusyDeviceTS; // when the currently busy device will stop being busy.
    u32 CurOpenBus; // TODO: is this needed?
    u8 BusyDevice; // The most recently accessed device.
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
    timestamp BurstStartTS; // when the current burst began; used for enforcing the burst length limit
    timestamp BusyTS; // when a new burst can begin; used for the determining when a new burst can next start
    timestamp LastAccessTS; // when the last access ended; used for main ram prefetching, and sequential access handling
    bool WeirdStartAddr; // used for handling a quirk where main ram bursts will restart with certain start pos alignments
    bool LastWasRead; // if the last access was a read; used for some jank with mainram -> mainram dma ig
    bool LastWasARM9; // used for which cpu was the last to go

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

    //timestamp* ARM9ThreatTimestamp;
    //timestamp* ARM7ThreatTimestamp;
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

[[nodiscard]] u32 AHB9_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq, const bool timings);
void AHB9_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq, const bool timings);
[[nodiscard]] u32 AHB7_Read(struct Console* sys, timestamp* ts, u32 addr, const u32 mask, const bool atomic, const bool hold, bool* seq, const bool timings);
void AHB7_Write(struct Console* sys, timestamp* ts, u32 addr, const u32 val, const u32 mask, const bool atomic, bool* seq, const bool timings);
bool AHB_NegOwnership(struct Console* sys, timestamp* cur, const bool atomic, const bool a9);
