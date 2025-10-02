#pragma once

#include <stddef.h>
#include "../../utils.h"
#include "../shared/arm.h"



// Full Model Name: ARM946E-S r1p1

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


// These could've just been magic numbers...
// But nooooo I wanted to make all the stuff cool and configurable for ??? reason.

// TCM constants:
// Physical sizes
constexpr unsigned ARM9_DTCMSize = KiB(16);
constexpr unsigned ARM9_ITCMSize = KiB(32);
// Sizes used by the CP15 registers.
constexpr unsigned ARM9_CP15DTCMSize = ((ARM9_DTCMSize > 0) ? (CTZ_CONSTEXPR(ARM9_DTCMSize / KiB(1)) + 1) : 0);
constexpr unsigned ARM9_CP15ITCMSize = ((ARM9_ITCMSize > 0) ? (CTZ_CONSTEXPR(ARM9_ITCMSize / KiB(1)) + 1) : 0);
constexpr u32 ARM9_TCMSizeReg = (ARM9_CP15DTCMSize << 18) // DTCM Size
                              | ((ARM9_DTCMSize == 0) << 14) // DTCM Absent
                              | (ARM9_CP15ITCMSize << 6) // ITCM Size
                              | ((ARM9_DTCMSize == 0) << 2); // "ITCM" Absent | ARM946E-S errata: This actually reports the DTCM status...

// Data cache constants:
constexpr unsigned ARM9_DCacheLineLength = 8; // words per line
constexpr unsigned ARM9_DCacheAssoc = 4; // Cache associativity; aka: lines per set
constexpr unsigned ARM9_DCacheSize = KiB(4);
constexpr unsigned ARM9_DCacheIndices = ARM9_DCacheSize / ARM9_DCacheAssoc / ARM9_DCacheLineLength / 4; // 32
constexpr unsigned ARM9_DTagNum = ARM9_DCacheIndices * ARM9_DCacheAssoc; // 128 tags

// Instruction cache constants:
constexpr unsigned ARM9_ICacheLineLength = 8; // words per line
constexpr unsigned ARM9_ICacheAssoc = 4; // Cache associativity; aka: lines per set
constexpr unsigned ARM9_ICacheSize = KiB(8);
constexpr unsigned ARM9_ICacheIndices = ARM9_ICacheSize / ARM9_ICacheAssoc / ARM9_ICacheLineLength / 4; // 64
constexpr unsigned ARM9_ITagNum = ARM9_ICacheIndices * ARM9_ICacheAssoc; // 256 tags

// i dont think other values are supported?
static_assert(ARM9_DCacheLineLength == 8);
static_assert(ARM9_ICacheLineLength == 8);
// if i ever get bored maybe i'll support direct mapped caches
// not sure if these support values other than 4 or 1?
static_assert(ARM9_DCacheAssoc == 4);
static_assert(ARM9_ICacheAssoc == 4);
// keeping logic simple means these must be a power of 2.
// sizes of 1KiB, 2KiB and >1MiB dont seem to be supported officially but I dont think there's any obvious reason up to 16MiB couldn't work?
static_assert((POPCNT_CONSTEXPR(ARM9_DTCMSize) <= 1) || (ARM9_DTCMSize > 16));
static_assert((POPCNT_CONSTEXPR(ARM9_ITCMSize) <= 1) || (ARM9_ITCMSize > 16));
static_assert((POPCNT_CONSTEXPR(ARM9_DCacheSize) <= 1) || (ARM9_DCacheSize > 16));
static_assert((POPCNT_CONSTEXPR(ARM9_ICacheSize) <= 1) || (ARM9_ICacheSize > 16));

constexpr u32 ARM9_CacheTypeReg = (7 << 25) // Cache Type: (apparently in our case indicates: "cache-clean-step operation", "cache-flush-step operation", and "lock-down facilities".)
                                | (1 << 24) // 1 = Harvard (Separate i/d caches) | 0 = Unified (One shared cache) | ARM946E-S only supports Harvard architecture(?)
                                | (((ARM9_DCacheSize > 0) ? (CTZ_CONSTEXPR(ARM9_DCacheSize / KiB(1)) + 1) : 0) << 18) // dcache size
                                | (((ARM9_DCacheSize > 0) ? CTZ_CONSTEXPR(ARM9_DCacheAssoc) : 0) << 15) // dcache Assoc
                                | ((ARM9_DCacheSize == 0) << 14) // dcache absent
                                | (2 << 12) // dcache line length (TODO: what does this mean exactly??)
                                | (((ARM9_ICacheSize > 0) ? (CTZ_CONSTEXPR(ARM9_ICacheSize / KiB(1)) + 1) : 0) << 6) // icache size
                                | (((ARM9_ICacheSize > 0) ? CTZ_CONSTEXPR(ARM9_ICacheAssoc) : 0) << 3) // icache Assoc
                                | ((ARM9_ICacheSize == 0) << 2) // icache absent
                                | (2 << 0); // icache line length (TODO: what does this mean exactly??)

constexpr u32 ARM9_IDCodeReg = (0x41 << 24) // implementor code (0x41 == ARM)
                             | (0x0 << 20) // variant (reserved...?)
                             | (0x5 << 16) // ARMv5TE
                             | (0x946 << 4) // brown, yelloy, and skyblue, don't tell me you already forgot?
                             | (0x1 << 0); // revision (r1p1)

// most bits in a cache tag are "fixed" (and presumably not real)
// so we can simplify their representations to optimize them for faster cache lookups.
union ARM9_ICacheTagsInternal
{
    u32 Raw;
    struct
    {
        bool Valid : 1;
        u32 TagBits : 32 - (CTZ_CONSTEXPR(ARM9_DCacheIndices) + CTZ_CONSTEXPR(ARM9_DCacheAssoc) + 3);
    };
};

// format used by the CPU for tag read/write commands.
union ARM9_ICacheTagsExternal
{
    u32 Raw;
    struct
    {
        u32 Set : CTZ_CONSTEXPR(ARM9_ICacheAssoc);
        u32 AlwaysClear : 2; // Dirty tags do not exist for ICache
        bool Valid : 1;
        u32 Index : CTZ_CONSTEXPR(ARM9_ICacheIndices);
        u32 TagBits : 32 - (CTZ_CONSTEXPR(ARM9_ICacheIndices) + CTZ_CONSTEXPR(ARM9_ICacheAssoc) + 3);
    };
};

// most bits in a cache tag are "fixed" (and presumably not real)
// so we can simplify their representations to optimize them for faster cache lookups.
union ARM9_DCacheTagsInternal
{
    u32 Raw;
    struct
    {
        u32 Set : CTZ_CONSTEXPR(ARM9_DCacheAssoc);
        bool DirtyLo : 1; // Lower half of cache line is dirty
        bool DirtyHi : 1; // Upper half of cache line is dirty
        bool Valid : 1;
        u32 Index : CTZ_CONSTEXPR(ARM9_DCacheIndices);
        u32 TagBits : 32 - (CTZ_CONSTEXPR(ARM9_DCacheIndices) + CTZ_CONSTEXPR(ARM9_DCacheAssoc) + 3);
    };
};

// format used by the CPU for tag read/write commands.
union ARM9_DCacheTagsExternal
{
    u32 Raw;
    struct
    {
        bool DirtyLo : 1; // Lower half of cache line is dirty
        bool DirtyHi : 1; // Upper half of cache line is dirty
        bool Valid : 1;
        u32 TagBits : 32 - (CTZ_CONSTEXPR(ARM9_DCacheIndices) + CTZ_CONSTEXPR(ARM9_DCacheAssoc) + 3);
    };
};

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

union ARM9_CacheLockdownCR
{
    u32 Raw;
    struct
    {
        u32 Segment : 2;
        u32 : 29;
        bool LoadBit : 1;
    };
};

union ARM9_RegionCR
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
    SPSR -> ABT SPSR
    r8-14: USR BANK

    mode: 0x8, 0x9, 0xA
    SPSR -> UND SPSR
    r8-r14: USR BANK

    mode: 0xC, 0xD, 0xE
    SPSR -> CPSR
    r8-r14: USR BANK
*/

struct ARM946ES
{
    struct ARM ARM;
    timestamp StreamTimes[8];
    alignas(32) s8 RegIL[16][2]; // r15 shouldn't be able to interlock?
    timestamp MemTimestamp; // used for memory stage and data bus tracking
    // used for thumb upper halfword fetches
    u16 LatchedHalfword;
    bool BoostedClock; /*   Determines whether the ARM9 is running at 4 or 2 times the bus clock.
                        *   Should only apply to the DSi bus.
                        *   true  = 4x
                        *   false = 2x.
                        *   Checkme: Is it faster to do this branchless?
                        *   Checkme: Can 3DS get an 8x or 1x clock multiplier with some jank?
                        */
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
                u32 FixedOnes : 4; // corresponds to: write buffer, 32 bit exceptions, no 26 bit address faults, late abort model.
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
        u8 WriteBufferConfig; // "This register only applies to data accesses." WHAT DOES THAT EVEN MEAN?????
        u32 DataPermsReg;
        u32 InstrPermsReg;
        //u32 MPURegionCR[8];
        union ARM9_RegionCR MPURegionCR[8];
        union ARM9_CacheLockdownCR DCacheLockdownCR;
        union ARM9_CacheLockdownCR ICacheLockdownCR;
        union ARM9_RegionCR DTCMCR;
        union ARM9_RegionCR ITCMCR;
        u32 TraceProcIdReg; // Trace Process Identifer Register; NOTE: this is output externally on ARM9 pins.
        // TODO: Add BIST regs.
        u8 TraceProcCR; // Trace Process Control Reg;
        u8 DTCMShift;
        u8 ITCMShift;
        u64 DTCMReadBase;
        u64 DTCMWriteBase;
    } CP15; // Coprocessor 15; System Control.
    MEMORY(DTCM, ARM9_DTCMSize);
    MEMORY(ITCM, ARM9_ITCMSize);
    MEMORY(DCache, ARM9_DCacheSize);
    MEMORY(ICache, ARM9_ICacheSize);
    alignas(ARM9_DCacheAssoc*4) union ARM9_DCacheTagsInternal DTagRAM[ARM9_DTagNum];
    alignas(ARM9_ICacheAssoc*4) union ARM9_ICacheTagsInternal ITagRAM[ARM9_ITagNum];
};

// ensure casting between the two types works as expected
static_assert(offsetof(struct ARM946ES, ARM) == 0);

extern void (*ARM9_InstructionLUT[0x1000])(struct ARM*, u32);
extern s8 (*ARM9_InterlockLUT[0x1000])(struct ARM946ES*, u32);
extern void (*THUMB9_InstructionLUT[0x1000])(struct ARM*, u16);
extern s8 (*THUMB9_InterlockLUT[0x1000])(struct ARM946ES*, u16);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void ARM9_Init(struct ARM946ES* ARM9, struct Console* sys);

// TEMP: debugging
void ARM9_Log(struct ARM946ES* ARM9);

// special exceptions.
void ARM9_Reset(struct ARM946ES* ARM9, const bool itcm, const bool hivec);
void ARM9_DataAbort(struct ARM946ES* ARM9);
void ARM9_InterruptRequest(struct ARM946ES* ARM9);
// only used by debugger hardware.
void ARM9_FastInterruptRequest(struct ARM946ES* ARM9);

// executed exceptions.
void ARM9_UndefinedInstruction(struct ARM* cpu, const u32 instr_data);
void ARM9_SoftwareInterrupt(struct ARM* ARM, const u32 instr_data);
void ARM9_PrefetchAbort(struct ARM* ARM, const u32 instr_data);
// stubs to make the compiler shut up
void THUMB9_UndefinedInstruction(struct ARM* ARM, const u16 instr_data);
void THUMB9_SoftwareInterrupt(struct ARM* ARM, const u16 instr_data);
void THUMB9_PrefetchAbort(struct ARM* ARM, const u16 instr_data);

// read register.
[[nodiscard]] u32 ARM9_GetReg(struct ARM946ES* ARM9, const int reg);
// write register.
// also sets up interlocks.
void ARM9_SetReg(struct ARM946ES* ARM9, const int reg, u32 val, const s8 iloffs, const s8 iloffs_c);
// write program counter (r15).
void ARM9_SetPC(struct ARM946ES* ARM9, u32 addr, const s8 iloffs);

[[nodiscard]] union ARM_PSR ARM9_GetSPSR(struct ARM946ES* ARM9);
// NOTE: this has 0 sanity checking for the inputs.
void ARM9_SetSPSR(struct ARM946ES* ARM9, union ARM_PSR psr);

// decrement interlock waits.
void ARM9_UpdateInterlocks(struct ARM946ES* ARM9, const s8 diff);
// cycledelay: time between the instruction beginning and register being fetched; used for interlock handling
// portc: refers to the port used to read from the register bank, does not allow for forwarding from certain instructions resulting in different interlock conditions
void ARM9_CheckInterlocks(struct ARM946ES* ARM9, s8* stall, const int reg, const s8 cycledelay, const bool portc);
// handle fetch stage cycles.
void ARM9_FetchCycles(struct ARM946ES* ARM9, const int fetch);
// add execute and memory stage cycles.
void ARM9_ExecuteCycles(struct ARM946ES* ARM9, const int execute, const int memory);

// run the next step of execution
void ARM9_Step(struct ARM946ES* ARM9);

[[nodiscard]] u32 ARM9_InstrRead32(struct ARM946ES* ARM9, u32 addr); // arm
[[nodiscard]] u16 ARM9_InstrRead16(struct ARM946ES* ARM9, u32 addr); // thumb
void ARM9_Uncond(struct ARM* cpu, const u32 instr_data); // idk where to put this tbh

void ARM9_ConfigureITCM(struct ARM946ES* ARM9);
void ARM9_ConfigureDTCM(struct ARM946ES* ARM9);
