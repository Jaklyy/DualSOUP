#pragma once

#include <stddef.h>
#include "../../utils.h"
#include "../arm_shared/arm.h"



// Full Model Name: ARM946E-S r1p1

/*  misc notes:
    runs at 4x base clock on NDS
    (optionally) 8x base clock on DSi/3DS
*/

/*
    Name decodes as:
    9: brown
    4: yelloy
    6: skyblue
    e: i think it means dsp extensions???? idk arm was high or something
    s: synesthesia i mean synthesizable
    r1: revision 1 (starts at 0)
    p1: fuck you
*/


// Physical size of ARM9 TCMs
constexpr int ITCM_PhySize = 32*1024; // 32 KiB
constexpr int DTCM_PhySize = 16*1024; // 16 KiB
// Cache variables
constexpr int Cache_LineLength       = 5; // 32 bytes / 8 words per line
constexpr int Cache_SetAssociativity = 2; // 4 lines per set
// Number of sets for each cache type
constexpr int ICache_Sets = 6; // 64 sets
constexpr int DCache_Sets = 5; // 32 sets
// Physical cache sizes
constexpr int ICache_Size = 1 << (ICache_Sets + Cache_LineLength + Cache_SetAssociativity); // 8 KiB
constexpr int DCache_Size = 1 << (DCache_Sets + Cache_LineLength + Cache_SetAssociativity); // 4 KiB

enum ARM9_InternalBuses : u8
{
    A9Bus_Free,  // bus is not being used
    A9Bus_Instr, // instruction bus
    A9Bus_Data,  // data bus
    A9Bus_Write, // write buffer (might have a distinct bus path of its own...?)
};

enum ARM_BurstType : u8
{
    ARMBus_Read8u,  // 8 bit read (unsigned)
    ARMBus_Read8s,  // 8 bit read (sign extend)
    ARMBus_Read16u, // 16 bit read (unsigned)
    ARMBus_Read16s, // 16 bit read (sign extend)
    ARMBus_Read32,  // single read (ROR)
    ARMBus_Read32b, // burst read (no ROR)
    ARMBus_Write8,  // 8 bit write
    ARMBus_Write16, // 16 bit write
    ARMBus_Write32, // 32 bit write
    ARMBus_Swap8,   // Atomic 8 bit read & write
    ARMBus_Swap32,  // Atomic 32 bit read & write
};

enum ARM_BurstWidth : u8
{
    ARMBus_8,
    ARMBus_16,
    ARMBus_32, // supports bursts
};

struct ARM9_BusQueue
{
    u8 BurstType;
    u8 BurstWidth;
    u8 BurstLen; // how many more fetches are remaining. max 16.
    union
    {
        u16 RList; // bitfield storing each register to be read from or written to
                   // used for loads and stores
        struct
        {
            u8 Read;
            u8 Write;
        };
    };
};

union CacheLockdownCR
{
    u32 Raw;
    struct
    {
        u32 Segment : 2;
        u32 : 29;
        bool LoadBit : 1;
    };
};

union RegionCR
{
    u32 Raw;
    struct
    {
        bool Enable : 1;
        u32 Size : 5;
        u32 : 6;
        u32 BaseAddr : 20;
    };
};

/*
    arm9 invalid modes:
    mode: 0x4, 0x5, 0x6
    SPSR -> ABT SPSR?
    r8-14: USR REGS?

    mode: 0x8, 0x9, 0xA
    SPSR -> ???
    r8-r14: USR?

    mode: 0xC, 0xD, 0xE
    SPSR -> CPSR
    r8-r14: USR?
*/

struct ARM946ES
{
    struct ARM ARM;
    alignas(32) s8 RegIL[16][2]; // r15 shouldn't be able to interlock?
    bool BoostedClock; /*   Determines whether the ARM9 is running at 4 or 2 times the bus clock.
                        *   Should only apply to the DSi bus.
                        *   true  = 4x
                        *   false = 2x.
                        *   Checkme: Is it faster to do this branchless?
                        *   Checkme: Can 3DS get an 8x or 1x clock multiplier with some jank?
                        */
    bool IStreamWait;
    bool SuperIStreamStall;
    timestamp MemTimestamp;
    // cache streaming address trackers, negative values mean inactive.
    s64 NextIStream;
    s64 NextDStream;
    // used for thumb upper halfword fetches
    u32 LatchedWord;
    u32 LatchedAddr;
    struct
    {
        union
        {
            u32 Raw;
            struct
            {
                bool MPUEnable : 1;
                bool : 1;
                bool DCacheEnable : 1;
                u32 : 4;
                bool BigEndian : 1;
                u32 : 4;
                bool ICacheEnable : 1;
                bool HiVector : 1; // high exception vector.
                bool CacheRR : 1; // round robin cache replacement algorithm.
                bool NoLoadTBit : 1; // prevents ldr/ldm from branching to thumb.
                bool DTCMEnable : 1;
                bool DTCMLoadMode : 1; // write only.
                bool ITCMEnable : 1;
                bool ITCMLoadMode : 1; // write only.
            };
        } CR; // Control Register.
        u8 DCacheConfig;
        u8 ICacheConfig;
        u8 WriteBufferConfig;
        u32 DataPermsReg;
        u32 InstrPermsReg;
        //u32 MPURegionCR[8];
        union RegionCR MPURegionCR[8];
        union CacheLockdownCR DCacheLockdownCR;
        union CacheLockdownCR ICacheLockdownCR;
        union RegionCR DTCMCR;
        union RegionCR ITCMCR;
        u32 TPIReg; // Trace Process Identifer Register; NOTE: this is output externally on ARM9 pins.
        // TODO: Add BIST regs.
    } CP15; // Coprocessor 15; System Control.
};

// ensure casting between the two types works as expected
static_assert(offsetof(struct ARM946ES, ARM) == 0);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void ARM9_Init(struct ARM946ES* ARM9);

// reset vector exception.
void ARM9_Reset(struct ARM946ES* ARM9);

// read register.
u32 ARM9_GetReg(struct ARM946ES* ARM9, const int reg);
// write register.
// also sets interlock updates.
void ARM9_SetReg(struct ARM946ES* ARM9, const int reg, u32 val, const int iloffs, const int iloffs_c);
// write program counter (r15).
void ARM9_SetPC(struct ARM946ES* ARM9, const int reg, u32 val, const int iloffs);

void ARM9_UpdateInterlocks(struct ARM946ES* ARM9, const s8 diff);
// cycledelay: time between the instruction beginning and register being fetched; used for interlock handling
// portc: refers to the port used to read from the register bank, does not allow for forwarding from certain instructions resulting in different interlock conditions
void ARM9_CheckInterlocks(struct ARM946ES* ARM9, int* offset, const int reg, const int cycledelay, const bool portc);
// add execute and memory stage cycles.
// also decrements the interlock waits.
void ARM9_ExecuteCycles(struct ARM946ES* ARM9, const u32 Execute, const u32 Memory);

// run the next step of execution
void ARM9_Step(struct ARM946ES* ARM9);
// finalize timings for the previous step of execution
void ARM9_ResolveTimings(struct ARM946ES* ARM9);

u32 ARM9_GetExceptionBase(struct ARM946ES* ARM9);
