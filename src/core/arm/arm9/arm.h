#pragma once

#ifdef __SSE2__
    #include <emmintrin.h>
    #include <string.h>
#endif
#include <stddef.h>
#include "core/utils.h"
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
constexpr u32 A946_DTCMSize = KiB(16);
constexpr u32 A946_ITCMSize = KiB(32);
// Sizes used by the CP15 registers.
constexpr u32 A946_CP15DTCMSize = ((A946_DTCMSize > 0) ? (CTZ_CONSTEXPR(A946_DTCMSize / KiB(1)) + 1) : 0);
constexpr u32 A946_CP15ITCMSize = ((A946_ITCMSize > 0) ? (CTZ_CONSTEXPR(A946_ITCMSize / KiB(1)) + 1) : 0);
constexpr u32 A946_TCMSizeReg = (A946_CP15DTCMSize << 18) // DTCM Size
                              | ((A946_DTCMSize == 0) << 14) // DTCM Absent
                              | (A946_CP15ITCMSize << 6) // ITCM Size
                              | ((A946_DTCMSize == 0) << 2); // "ITCM" Absent | ARM946E-S errata: This actually reports the DTCM status...

// Data cache constants:
constexpr u32 A946_DCacheLineLength = 8; // words per line
constexpr u32 A946_DCacheAssoc = 4; // Cache associativity; aka: lines per set
constexpr u32 A946_DCacheSize = KiB(4);
constexpr u32 A946_DCacheIndices = A946_DCacheSize / A946_DCacheAssoc / A946_DCacheLineLength / 4; // 32
constexpr u32 A946_DTagNum = A946_DCacheIndices * A946_DCacheAssoc; // 128 tags

// Instruction cache constants:
constexpr u32 A946_ICacheLineLength = 8; // words per line
constexpr u32 A946_ICacheAssoc = 4; // Cache associativity; aka: lines per set
constexpr u32 A946_ICacheSize = KiB(8);
constexpr u32 A946_ICacheIndices = A946_ICacheSize / A946_ICacheAssoc / A946_ICacheLineLength / 4; // 64
constexpr u32 A946_ITagNum = A946_ICacheIndices * A946_ICacheAssoc; // 256 tags

// i dont think other values are supported?
static_assert(A946_DCacheLineLength == 8);
static_assert(A946_ICacheLineLength == 8);
// if i ever get bored maybe i'll support direct mapped caches
// not sure if these support values other than 4 or 1?
static_assert(A946_DCacheAssoc == 4);
static_assert(A946_ICacheAssoc == 4);
// keeping logic simple means these must be a power of 2.
// sizes of 1KiB, 2KiB and >1MiB dont seem to be supported officially but I dont think there's any obvious reason up to 16MiB couldn't work?
static_assert((POPCNT_CONSTEXPR(A946_DTCMSize) <= 1) || (A946_DTCMSize > 16));
static_assert((POPCNT_CONSTEXPR(A946_ITCMSize) <= 1) || (A946_ITCMSize > 16));
static_assert((POPCNT_CONSTEXPR(A946_DCacheSize) <= 1) || (A946_DCacheSize > 16));
static_assert((POPCNT_CONSTEXPR(A946_ICacheSize) <= 1) || (A946_ICacheSize > 16));

constexpr u32 A946_CacheTypeReg = (7 << 25) // Cache Type: (apparently in our case indicates: "cache-clean-step operation", "cache-flush-step operation", and "lock-down facilities".)
                                | (1 << 24) // 1 = Harvard (Separate i/d caches) | 0 = Unified (One shared cache) | ARM946E-S only supports Harvard architecture(?)
                                | (((A946_DCacheSize > 0) ? (CTZ_CONSTEXPR(A946_DCacheSize / KiB(1)) + 1) : 0) << 18) // dcache size
                                | (((A946_DCacheSize > 0) ? CTZ_CONSTEXPR(A946_DCacheAssoc) : 0) << 15) // dcache Assoc
                                | ((A946_DCacheSize == 0) << 14) // dcache absent
                                | (2 << 12) // dcache line length (TODO: what does this mean exactly??)
                                | (((A946_ICacheSize > 0) ? (CTZ_CONSTEXPR(A946_ICacheSize / KiB(1)) + 1) : 0) << 6) // icache size
                                | (((A946_ICacheSize > 0) ? CTZ_CONSTEXPR(A946_ICacheAssoc) : 0) << 3) // icache Assoc
                                | ((A946_ICacheSize == 0) << 2) // icache absent
                                | (2 << 0); // icache line length (TODO: what does this mean exactly??)

constexpr u32 A946_IDCodeReg = (0x41 << 24) // implementor code (0x41 == ARM)
                             | (0x0 << 20) // variant (reserved...?)
                             | (0x5 << 16) // ARMv5TE
                             | (0x946 << 4) // brown, yelloy, and skyblue, don't tell me you already forgot?
                             | (0x1 << 0); // revision (r1p1)

// most bits in a cache tag are "fixed" (and presumably not real)
// so we can simplify their representations to optimize them for faster cache lookups.
typedef union
{
    u32 Raw;
    struct
    {
        bool Valid : 1;
        u32 TagBits : 32 - (CTZ_CONSTEXPR(A946_ICacheIndices) + CTZ_CONSTEXPR(A946_ICacheAssoc) + CTZ_CONSTEXPR(A946_ICacheLineLength));
    };
} A946_ICacheTagsInternal;

// format used by the CPU for tag read/write commands.
typedef union
{
    u32 Raw;
    struct
    {
        u32 Set : CTZ_CONSTEXPR(A946_ICacheAssoc);
        u32 AlwaysClear : 2; // Dirty tags do not exist for ICache
        bool Valid : 1;
        u32 Index : CTZ_CONSTEXPR(A946_ICacheIndices);
        u32 TagBits : 32 - (CTZ_CONSTEXPR(A946_ICacheIndices) + CTZ_CONSTEXPR(A946_ICacheAssoc) + CTZ_CONSTEXPR(A946_ICacheLineLength));
    };
} A946_ICacheTagsExternal;

// most bits in a cache tag are "fixed" (and presumably not real)
// so we can simplify their representations to optimize them for faster cache lookups.
typedef union
{
    u32 Raw;
    struct
    {
        bool DirtyLo : 1; // Lower half of cache line is dirty
        bool DirtyHi : 1; // Upper half of cache line is dirty
        bool Valid : 1;
        u32 TagBits : 32 - (CTZ_CONSTEXPR(A946_DCacheIndices) + CTZ_CONSTEXPR(A946_DCacheAssoc) + CTZ_CONSTEXPR(A946_DCacheLineLength));
    };
} A946_DCacheTagsInternal;

// format used by the CPU for tag read/write commands.
typedef union
{
    u32 Raw;
    struct
    {
        u32 Set : CTZ_CONSTEXPR(A946_DCacheAssoc);
        bool DirtyLo : 1; // Lower half of cache line is dirty
        bool DirtyHi : 1; // Upper half of cache line is dirty
        bool Valid : 1;
        u32 Index : CTZ_CONSTEXPR(A946_DCacheIndices);
        u32 TagBits : 32 - (CTZ_CONSTEXPR(A946_DCacheIndices) + CTZ_CONSTEXPR(A946_DCacheAssoc) + CTZ_CONSTEXPR(A946_DCacheLineLength));
    };
} A946_DCacheTagsExternal;

typedef union
{
    u32 Raw;
    struct
    {
        u32 Segment : 2;
        u32 : 29;
        bool LoadBit : 1;
    };
} A946_CacheLockdownCR;

typedef union
{
    u32 Raw;
    struct
    {
        bool Enable : 1;
        u32 Size : 5;
        u32 : 6;
        u32 BaseAddr : 20;
    };
} A946_RegionCR;

typedef enum : u8
{
    A946WB_8,
    A946WB_16,
    A946WB_32,
    A946WB_Addr,
} A946_WBufferFlags;

typedef struct
{
    alignas(u64)
    u32 Data;
    A946_WBufferFlags Flags;
} A946_WBufferFIFO;

typedef struct
{
    A946_WBufferFIFO FIFOEntry[16];
    u32 Addr;
    u8 FIFOFillPtr;
    u8 FIFODrainPtr;
    bool Empty;
} A946_WBuffer; // Write Buffer

typedef enum : u8
{
    A946BIU_DataNone,
    A946BIU_DataCache,
} A946_BIUDType;

typedef enum : u8
{
    A946BIU_InstrNone,
    A946BIU_InstrCache,
    A946BIU_InstrSingle,
} A946_BIUIType;

typedef struct
{
    A946_WBuffer WBuffer;
    u32 WriteVal[16]; // stm can do up to 16 values at a time
    u32 DataAddr;
    u32 InstrAddr;
    A946_BIUDType DataType;
    u8 DataMax;
    u8 DataCur;
    A946_BIUIType InstrType;
    u8 InstrCur;
    u8 InstrMax;
} A946_BIU; // Bus Interface Unit

typedef struct
{
    bool Read : 1;
    bool Write : 1;
    bool Exec : 1;
    bool ICache : 1;
    bool DCache : 1;
    bool Buffer : 1;
} A946_MPUPerms;

typedef enum : u8
{
    A9InstrBus_Abort,
    A9InstrBus_ITCM,
    A9InstrBus_ICache,
    A9InstrBus_BIU,
} A946_InstrBus;

typedef enum : u8
{
    A9BusDefer_None,
    A9BusDefer_Load,
    A9BusDefer_Store,
} A946_BusDefer;

#ifdef __SSE2__
    #define A946_ICacheSetLookup \
        /* TODO: consider unhardcoding this shit */ \
        /* isolate index */ \
        u32 index = (addr & 0x000007E0) >> 3; \
        /* isolate tag and set valid bit */ \
        /* this will be used for lookup */ \
        u32 tagcmp = (addr >> 10) | 1; \
        \
        /* lookup valid set */ \
        __m128i tags; memcpy(&tags, &a946->ITagRAM[index].Raw, sizeof(tags)); \
        __m128i cmp = _mm_set1_epi32(tagcmp); \
        cmp = _mm_cmpeq_epi32(tags, cmp); \
        u8 set = stdc_trailing_zeros((u32)_mm_movemask_ps(_mm_castsi128_ps(cmp)));

    #define A946_DCacheSetLookup \
        /* TODO: consider unhardcoding this shit */ \
        /* isolate index */ \
        u32 index = (addr & 0x000003E0) >> 3; \
        /* isolate tag and set valid bit */ \
        /* this will be used for lookup */ \
        u32 tagcmp = (addr >> 9) | 1; \
        \
        /* lookup valid set */ \
        __m128i tags; memcpy(&tags, &a946->DTagRAM[index].Raw, sizeof(tags)); \
        __m128i cmp = _mm_set1_epi32(tagcmp); \
        /* note: we need to shift out the dirty flags before comparing */ \
        tags = _mm_srli_epi32(tags, 2); \
        cmp = _mm_cmpeq_epi32(tags, cmp); \
        u8 set = stdc_trailing_zeros((u32)_mm_movemask_ps(_mm_castsi128_ps(cmp)));
#else
    #define A946_ICacheSetLookup \
        /* TODO: consider unhardcoding this shit */ \
        /* isolate index */ \
        u32 index = (addr & 0x000007E0) >> 3; \
        /* isolate tag and set valid bit */ \
        /* this will be used for lookup */ \
        u32 tagcmp = (addr >> 10) | 1; \
        \
        /* lookup valid set */ \
        u8 set = A946_ICacheAssoc; \
        for (unsigned i = 0; i < A946_ICacheAssoc; i++) \
        { \
            if (a946->ITagRAM[index+i].Raw == tagcmp) \
            { \
                set = i; \
                break; \
            } \
        }

    #define A946_DCacheSetLookup \
        /* TODO: consider unhardcoding this shit */ \
        /* isolate index */ \
        u32 index = (addr & 0x000003E0) >> 3; \
        /* isolate tag and set valid bit */ \
        /* this will be used for lookup */ \
        u32 tagcmp = (addr >> 9) | 1; \
        \
        /* lookup valid set */ \
        u8 set = A946_DCacheAssoc; \
        for (unsigned i = 0; i < A946_DCacheAssoc; i++) \
        { \
            /* note: we need to shift out the dirty flags before comparing */ \
            if ((a946->DTagRAM[index+i].Raw >> 2) == tagcmp) \
            { \
                set = i; \
                break; \
            } \
        }
#endif

#define A9ClockShift(a9) ((a9).BoostedClock ? 2 : 1)
#define A9BusLatency(a9) ((a9).BoostedClock ? 2 : 3) // TODO: there's probably more meaningful logic behind this
#define A9ClockRound(a9) ((a9).BoostedClock ? 3 : 1)

/*
    arm9e-s invalid modes:
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

typedef enum : u8
{
    A946_BusNone,
    A946_BusDone,
    A946_BusGo,
    A946_BusBusy,
} A946_InternalBusFlags;

typedef struct
{
    ARM ARM;
    A946_BIU BIU;
    union
    {
        struct
        {
            s8 Cur; // test as no interlock via: !(& 0x10)
            s8 Next; // should always be set as: (reg | 0x80)
        };
        s16 Raw; // move Next to Cur via (>>= 8) to automatically set as none via sign extension.
    } RegIL; // should be initialized as -1
    A946_InternalBusFlags IBus;
    A946_InternalBusFlags DBus;
    s8 IStreamWait;
    s8 DStreamWait;
    u16 IStreamIndex;
    u16 DStreamIndex;
    s8 WriteBufferWait;

    u32 InstrLatch; // used for thumb upper halfword fetches (speculative: 32 bit?)
    bool ITCMMultiplexData; // is itcm multiplexer set to data?
#if 0
    bool BoostedClock; /*   Determines whether the ARM9 is running at 4 or 2 times the bus clock.
                        *   Should only apply to the DSi bus.
                        *   true  = 4x
                        *   false = 2x.
                        *   Checkme: Is it faster to do this branchless?
                        *   Checkme: Can 3DS get an 8x or 1x clock multiplier with some jank?
                        */
#endif
    u32 DeferredMask;
    timestamp InstrTS;
    timestamp DataTS;
    timestamp DataWrStall;
    A946_InstrBus InstrBus; // cached
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
        A946_RegionCR MPURegionCR[8];
        A946_CacheLockdownCR DCacheLockdownCR;
        A946_CacheLockdownCR ICacheLockdownCR;
        A946_RegionCR DTCMCR;
        A946_RegionCR ITCMCR;
        u32 TraceProcIdReg; // Trace Process Identifer Register; NOTE: this is output externally on ARM946E-S pins.
        // TODO: Add BIST regs.
        u8 TraceProcCR; // Trace Process Control Reg;
        u8 DTCMShift;
        u8 ITCMShift;
        u64 DTCMReadBase;
        u64 DTCMWriteBase;
        alignas(sizeof(A946_MPUPerms)*8) A946_MPUPerms MPURegionPermsUser[8];
        alignas(sizeof(u32)*8) u32 MPURegionBase[8];
        alignas(sizeof(u32)*8) u32 MPURegionMask[8];
        alignas(sizeof(A946_MPUPerms)*8) A946_MPUPerms MPURegionPermsPriv[8];
        u64 DCachePRNG;
        u64 ICachePRNG;
    } CP15; // Coprocessor 15; System Control.
    MEMORY(DTCM, A946_DTCMSize);
    MEMORY(ITCM, A946_ITCMSize);
    MEMORY(DCache, A946_DCacheSize);
    MEMORY(ICache, A946_ICacheSize);
    alignas(A946_DCacheAssoc*4) A946_DCacheTagsInternal DTagRAM[A946_DTagNum];
    alignas(A946_ICacheAssoc*4) A946_ICacheTagsInternal ITagRAM[A946_ITagNum];
} ARM946ES;

// ensure casting between the two types works as expected
static_assert(offsetof(ARM946ES, ARM) == 0);

extern void (*A9ES_InstructionLUT[0x1000])(ARM*, ARM_Instr);
extern s8 (*A9ES_InterlockLUT[0x1000])(ARM946ES*, ARM_Instr, s8, s8, s8);
extern void (*T9ES_InstructionLUT[64])(ARM*, ARM_Instr);
extern s8 (*T9ES_InterlockLUT[64])(ARM946ES*, ARM_Instr, s8, s8, s8);

// run to initialize the cpu.
// assumes everything was zero'd out.
// should be akin to a cold boot?
void A946_Init(ARM946ES* a946, Console* sys);

// ARM9 handler entrypoint
void A946_MainLoop(ARM946ES* a946);

// TEMP: debugging
void A946_Log(ARM946ES* a946);

// signal exceptions.
void A946_Reset(ARM946ES* a946, const bool itcm, const bool hivec);
void A9ES_DataAbort(ARM946ES* a9es);
void A9ES_InterruptRequest(ARM946ES* a9es);
void A9ES_FastInterruptRequest(ARM946ES* a9es); // only used by debugger hardware on gba/nds/dsi/3ds.

// executed exceptions.
void A9ES_RaiseUDF(ARM* arm, const ARM_Instr instr_data, const s32 execycles);
void A9ES_UndefinedInstruction(ARM* arm, const ARM_Instr instr_data);
void A9ES_SupervisorCall(ARM* arm, const ARM_Instr instr_data); // aka: software interrupt
void A9ES_PrefetchAbort(ARM* arm, const ARM_Instr instr_data);
// stubs to make the compiler shut up
void T9ES_UndefinedInstruction(ARM* arm, const ARM_Instr instr_data);
void T9ES_SupervisorCall(ARM* arm, const ARM_Instr instr_data); // aka: software interrupt
void T9ES_PrefetchAbort(ARM* arm, const ARM_Instr instr_data);

// setters and getters
[[nodiscard]] u32 A9ES_GetReg(ARM946ES* a9es, const s32 reg); // read register.
void A9ES_SetReg(ARM946ES* a9es, const s32 reg, u32 val); // write register.
void A9ES_SetPC(ARM946ES* a9es, u32 addr); // write program counter (r15).
[[nodiscard]] ARM_PSR A9ES_GetSPSR(ARM946ES* a9es);
void A9ES_SetSPSR(ARM946ES* a9es, ARM_PSR psr); // NOTE: this has no sanity checking for the inputs.

// interlock handlers
s8 A9ES_DecodeInterlocks(ARM946ES* a9es, const bool thumb, const s8 reg, const s8 len, const s8 len_c);
inline void A9ES_SetTwoCycleInterlock(ARM946ES* a9es, const u8 reg);

// add execute stage cycles.
void A9ES_ExecuteCycles(ARM946ES* ARM9, const s32 execute);

void A946_UpdateInstrRegion(ARM946ES* a946); // should be run on nonsequentials or when crossing 4 KiB boundaries.
void A946_InstrRead(ARM946ES* a946, timestamp now);

// misc cleanup functions
void A946_InstrRead_Post(ARM946ES* a946, const u32 addr);
void A946_AddMemCycles(ARM946ES* a946);

// read/write handlers
[[nodiscard]] u32 A946_DataRead32(ARM946ES* a946, u32 addr, bool* seq, bool* dabt);
[[nodiscard]] u16 A946_DataRead16(ARM946ES* a946, u32 addr, bool* seq, bool* dabt);
[[nodiscard]] u32 A946_DataRead8(ARM946ES* a946, u32 addr, bool* seq, bool* dabt);
void A946_DataWrite32(ARM946ES* a946, u32 addr, u32 val, const bool atomic, const bool deferrable, bool* seq, bool* dabt);
void A946_DataWrite16(ARM946ES* a946, u32 addr, u32 val, bool* seq, bool* dabt);
void A946_DataWrite8(ARM946ES* a946, u32 addr, u32 val, const bool atomic, bool* seq, bool* dabt);
[[nodiscard]] bool A9ES_RotateExtendUnit(u32* val, const u32 addr, const ARM_DataWidth size, const bool signext, const bool bigendian);

void A9ES_Uncond(ARM* cpu, const ARM_Instr instr_data); // idk where to put this tbh

// mpu handlers
void A946_ConfigureITCM(ARM946ES* a946);
void A946_ConfigureDTCM(ARM946ES* a946);
void A946_ConfigureMPURegionSize(ARM946ES* a946, const u8 rgn);
void A946_ConfigureMPURegionPerms(ARM946ES* a946);

// cache handlers
bool A946_DCacheReadLookup(ARM946ES* a946, const u32 addr, timestamp now, u32* data);
bool A946_ICacheLookup(ARM946ES* a946, const u32 addr, timestamp now, u32* instr);

// Logging
void A946_DumpMPU(const ARM946ES* a946);
