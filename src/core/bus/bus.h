#pragma once
#include "core/utils.h"


typedef struct Console Console;

// used for modelling write contention
typedef enum : u8
{
    Dev_Null, // invalid
    Dev_Bios7, // ARM7 only
    //Dev_MainRAM,
    Dev_WRAM9,
    Dev_WRAM7,
    Dev_IO9,
    Dev_IO7,

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

    Dev_Max,
} NTRAHB_Devices;

static_assert(Dev_Max < 32, "BUSYDEVICE BIT MASK TOO SMALL!!!");

// note: actual IDs unknown
typedef enum : u8 // bitfield
{
    MAN9_DMA0,
    MAN9_DMA1,
    MAN9_DMA2,
    MAN9_DMA3,
    MAN9_NDMA0,
    MAN9_NDMA1,
    MAN9_NDMA2,
    MAN9_NDMA3,
    MAN9_ARM9,
    MAN9_MAX, // bus is idle (might be arm9 instead?)
} Bus9_Managers;

// note: actual IDs unknown
typedef enum : u8 // bitfield
{
    MAN7_SCAPDMA0,
    MAN7_SCAPDMA1,
    MAN7_SNDDMA0,
    MAN7_SNDDMA1,
    MAN7_SNDDMA2,
    MAN7_SNDDMA3,
    MAN7_SNDDMA4,
    MAN7_SNDDMA5,
    MAN7_SNDDMA6,
    MAN7_SNDDMA7,
    MAN7_SNDDMA8,
    MAN7_SNDDMA9,
    MAN7_SNDDMAA,
    MAN7_SNDDMAB,
    MAN7_SNDDMAC,
    MAN7_SNDDMAD,
    MAN7_SNDDMAE,
    MAN7_SNDDMAF,
    MAN7_DMA0,
    MAN7_DMA1,
    MAN7_DMA2,
    MAN7_DMA3,
    MAN7_NDMA0,
    MAN7_NDMA1,
    MAN7_NDMA2,
    MAN7_NDMA3,
    MAN7_ARM7,
    MAN7_MAX, // bus is idle (might be arm7 instead?)
} Bus7_Managers;

constexpr u8 MAN_NONE = u8_max;

typedef enum : u8 // 3 bit
{
    HSIZE_8,
    HSIZE_16,
    HSIZE_32,
    // below are defined in protocol, but almost certainly unimplemented on nds:
    HSIZE_64,
    HSIZE_128,
    HSIZE_256,
    HSIZE_512,
    HSIZE_1024,
} AHB_HSIZE;

typedef enum : u8 // 3 bit
{
    HBURST_SINGLE,  // len 1;
    HBURST_INCR,    // len min 1, max: "infinite"; should not cross a 1KiB boundary
    HBURST_WRAP4,   // len 4;   wraps on crossing (4<<HSIZE) byte boundaries
    HBURST_INCR4,   // len 4;   should not cross a 1KiB boundary
    HBURST_WRAP8,   // len 8;   wraps on crossing (8<<HSIZE) byte boundaries
    HBURST_INCR8,   // len 8;   should not cross a 1KiB boundary
    HBURST_WRAP16,  // len 16;  wraps on crossing (16<<HSIZE) byte boundaries
    HBURST_INCR16,  // len 16;  should not cross a 1KiB boundary
} AHB_HBURST;

typedef enum : u8 // 2 bit
{
    HTRANS_IDLE, // no data transfer required
    HTRANS_BUSY, // burst will continue but 
    HTRANS_NONSEQ, // first transfer of a burst
    HTRANS_SEQ, // indicates the burst is sequential and addr is (prev + (1<<HSIZE)) (unless wrapping burst is used); (not sure what happens if the address is incorrect?)
} AHB_HTRANS;

// okay is single cycle, all others need 2 cycles to signal
typedef enum : u8 // 2 bit
{
    HRESP_OKAY,  // 1 cycle. everything is fine.
    HRESP_ERROR, // 2 cycle. an error has occurred; manager can cancel remaining burst but is not required to do so.
    HRESP_RETRY, // 2 cycle. transfer not yet complete; manager should retry transfer. arbiter continues using normal priority scheme.
    HRESP_SPLIT, // 2 cycle. transfer not yet complete; manager should retry transfer. arbiter should set the awaiting manager to lowest priority until suborinate signals it is ready.
} AHB_HRESP;

typedef struct
{
    bool Data : 1; // instr = 0, data = 1
    bool Privileged : 1;
    bool Bufferable : 1;
    bool Cacheable : 1;
} AHB_HPROT;

typedef enum : u8
{
    CB_None,

    CB9_BIU9InstrNormal,
    CB9_BIU9InstrStream,

    CB9_BIU9WriteBuffer,

    CB9_BIU9DataNormal,
    CB9_BIU9DataStream,

    CB9_BIU9Idle,
} BusCallbacks;

typedef struct
{
    //timestamp Time; // when request occurs
    u32 Addr; // address bus value
    u32 WrVal; // write bus value
    //u32 writemask; // lanes to update write bus with (unk if anything doesn't update all lanes?)
    bool Write; // is operation a write
    bool Lock; // part of locked sequence
    union
    {
        Bus9_Managers Man9;
        Bus7_Managers Man7;
        u8 Man;
    };
    AHB_HPROT Prot; // protection flags
    AHB_HSIZE Size; // access width
    AHB_HTRANS Type; // access type
    BusCallbacks CB;
} BusReq;

// ARM7 Bus uses an unknown bus architecture, doesn't seem to be an ahb?
// notes:
// likely tri-state (using one set of lanes for both read and write data) (evidence: gba open bus is updated by reads and writes)
// seems to have fewer (todo: exact amount?) cycles of latency (vs the ahb's 3)
// likely unchanged from gba, most differences seem to be a result of the components hooked up to it having different behavior

// ARM9 bus seems to use a compliant(?) AMBA 2.0 AHB

typedef struct
{
    //timestamp LastFetchTs;
    BusReq PipeFIFO[4]; // granted, addr // CHECKME
    BusReq Reqs[((u8)MAN9_MAX > (u8)MAN7_MAX) ? MAN9_MAX : MAN7_MAX];
    u8 FIFOFillPtr;
    u8 FIFODrainPtr;
    bool FIFOEmpty;
    bool LockSched;
    timestamp PipeExitTs[4];
    union
    {
        Bus9_Managers HLock9; // which manager is locking the bus
        Bus7_Managers HLock7; // which manager is locking the bus
        u8 HLockGeneric;
    };
    u8 PipeCycles;
    u32 ReqList;
    // post data:
    u32 PostReadBus; // used by arm7 bus for open bus emulation
    BusCallbacks PostCB;
    bool PostNoPrev;
} BusImpl;

typedef enum : u8
{
    MainRAM_None,
    MainRAM_A9,
    MainRAM_A7,
} MainRAM_Buses;

// MainRAM is a type of FCRAM.
// gbatek lists the following chips as being used in retail DS models:
// Fujitsu 82DBS02163C-70L
// ST Microelectronics M69AB048BL70ZA8
// MainRAM only natively supports 16 bit accesses.
// not 100% clear what mechanic the nds uses to support 8 bit writes?
// 8bit reads presumably use some special logic or rely on the fact that our specific ARM processors
// seem to always perform a rotate right and bit masking operation on byte reads
typedef struct
{
    timestamp BurstLimitTs;
    timestamp LastFetchTs;
    u32 AddrLatch; // fcram chip internally latched address
    u32 AddrSubmMask; // mask when sending address to fcram chip
    u32 AddrLatchMask; // mask when stepping latched addr internally
    bool WeirdStart; // burst start address was within the last 3 halfwords of a 16 halfword boundary; forces an NS cycle when crossing the 16 halfword boundary
    bool PrevWrite;
    bool IsReq9;
    bool IsReq7;
    bool BurstActive;
    MainRAM_Buses CurReq;
    MainRAM_Buses Locked;
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
} BusMainRAM;

void Bus9_Init(BusImpl* bus);
void Bus7_Init(BusImpl* bus);

// shared handlers
void AddBusContention(Console* sys, const timestamp cur, const NTRAHB_Devices device);

// if the arm7 just woke up we may need to nudge the bus a little to make sure it handles its fetches now
void Bus7_A7Wake(Console* sys, const timestamp now);
// handlers
void Bus_Req(Console* sys, const BusReq* req, const timestamp now, const bool a9);
void Bus_Run(Console* sys, const timestamp now, const bool a9);
void Bus_TransferPost(Console* sys, const timestamp fin, const bool a9);
void Bus_TransferPostSetup(Console* sys, const u32 rdata, const bool isread, const timestamp end, const bool noprev, const bool cb, const bool a9);

void MainRAM_Run(Console* sys, const timestamp now);
void IO9_Handler(Console* sys, timestamp now);
void IO7_Handler(Console* sys, timestamp now);

void IO9_FinishDiv(Console* sys);
void IO9_FinishSqrt(Console* sys);

void WiFi_Init(Console* sys);
void WiFi_Read(Console* sys, u32* rdata, timestamp* now, const u32 addr, const AHB_HSIZE size);
void WiFi_Write(Console* sys, timestamp* now, const u32 addr, const u32 wrdata, const u32 mask);
