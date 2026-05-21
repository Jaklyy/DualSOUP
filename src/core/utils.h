#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>

#include <SDL3/SDL_mutex.h>

#include "../frontend/coroutine.h"




typedef int8_t s8;
#define s8_max (INT8_MAX)
#define s8_min (INT8_MIN)

typedef uint8_t u8;
#define u8_max (UINT8_MAX)


typedef int16_t s16;
#define s16_max (INT16_MAX)
#define s16_min (INT16_MIN)

typedef uint16_t u16;
#define u16_max (UINT16_MAX)


typedef int32_t s32;
#define s32_max (INT32_MAX)
#define s32_min (INT32_MIN)

typedef uint32_t u32;
#define u32_max (UINT32_MAX)


typedef int64_t s64;
#define s64_max (INT64_MAX)
#define s64_min (INT64_MIN)

typedef uint64_t u64;
#define u64_max (UINT64_MAX)


typedef u32 u32x4 __attribute__ ((vector_size(sizeof(u32)*4)));
typedef s32 s32x4 __attribute__ ((vector_size(sizeof(u32)*4)));


// in theory we could use smaller timestamps and use ckd_add and chk_sub to adjust all the timestamps when one overflows.
// this might incur a noticeable performance penalty on every single timestamp increment.
// but it would allow for the system to be infinitely running (mind you 727 years is probably a long enough time already)
// it may also allow for faster scheduling by being able to pack more scheduler timestamps into a single simd reg?
#ifdef REALTHREAD
typedef volatile uint64_t timestamp;
#else
typedef uint64_t timestamp;
#endif
#define timestamp_max (UINT64_MAX)

#define KiB(x) ((u64)(x) * 1024)
#define MiB(x) ((u64)(x) * 1024 * 1024)
#define GiB(x) ((u64)(x) * 1024 * 1024 * 1024)


// todo: actually add fallback paths if these dont exist for w/e reason
/*#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)*/

// attempt to support platforms that dont support stdbit.h for w/e reason
#if __has_include(<stdbit.h>)
    #include <stdbit.h>
#else
    //#warning "stdbit.h not detected, fallback options will be used."
#endif

#ifndef stdc_trailing_zeros
    //#warning "stdc_trailing_zeros not found, using fallback."
    static unsigned int ds_internal_stdctz64 [[maybe_unused]] (u64 input)
    {
        if (input == 0) return 64;
        else return __builtin_ctzll(input);
    }
    static unsigned int ds_internal_stdctz32 [[maybe_unused]] (u32 input)
    {
        if (input == 0) return 32;
        else return __builtin_ctz(input);
    }
    static unsigned int ds_internal_stdctz16 [[maybe_unused]] (u16 input)
    {
        if (input == 0) return 16;
        else return __builtin_ctz(input);
    }
    static unsigned int ds_internal_stdctz8 [[maybe_unused]] (u8 input)
    {
        if (input == 0) return 8;
        else return __builtin_ctz(input);
    }
    #define stdc_trailing_zeros(x) _Generic((x), \
        s8: ds_internal_stdctz8, u8: ds_internal_stdctz8, \
        s16: ds_internal_stdctz16, u16: ds_internal_stdctz16, \
        s32: ds_internal_stdctz32, u32: ds_internal_stdctz32, \
        s64: ds_internal_stdctz64, u64: ds_internal_stdctz64)((x))
#endif

#ifndef stdc_trailing_ones
    //#warning "stdc_trailing_ones not found, using fallback."
    #define stdc_trailing_ones(x) stdc_trailing_zeros(~(x))
#endif

#ifndef stdc_leading_zeros
    //#warning "stdc_leading_zeros not found, using fallback."
    static unsigned int ds_internal_stdclz64 [[maybe_unused]] (u64 input)
    {
        if (input == 0) return 64;
        else return __builtin_clzll(input);
    }
    static unsigned int ds_internal_stdclz32 [[maybe_unused]] (u32 input)
    {
        if (input == 0) return 32;
        else return __builtin_clz(input);
    }
    static unsigned int ds_internal_stdclz16 [[maybe_unused]] (u16 input)
    {
        if (input == 0) return 16;
        else return (32-16)-__builtin_clz(input);
    }
    static unsigned int ds_internal_stdclz8 [[maybe_unused]] (u8 input)
    {
        if (input == 0) return 8;
        else return (32-8)-__builtin_clz(input);
    }
    #define stdc_leading_zeros(x) _Generic((x), \
        s8: ds_internal_stdclz8, u8: ds_internal_stdclz8, \
        s16: ds_internal_stdclz16, u16: ds_internal_stdclz16, \
        s32: ds_internal_stdclz32, u32: ds_internal_stdclz32, \
        s64: ds_internal_stdclz64, u64: ds_internal_stdclz64)((x))
#endif

#ifndef stdc_leading_ones
    //#warning "stdc_leading_ones not found, using fallback."
    #define stdc_leading_ones(x) stdc_leading_zeros(~(x))
#endif

#ifndef stdc_count_ones
    //#warning "stdc_count_ones not found, using fallback."
    #define stdc_count_ones(x) __builtin_popcountll((u64)(x))
#endif

#ifndef stdc_count_zeros
    //#warning "stdc_count_zeros not found, using fallback."
    #define stdc_count_zeros(x) stdc_count_ones(~(x))
#endif

#define bswap(x) _Generic((x), \
    s16: __builtin_bswap16, u16: __builtin_bswap16, \
    s32: __builtin_bswap32, u32: __builtin_bswap32, \
    s64: __builtin_bswap64, u64: __builtin_bswap64)((x))

#define HOST_CACHEALIGN (64)

#define forceinline __attribute((always_inline)) inline


// the builtins are constexpr but the actual standard defined functions aren't...
#define CTZ_CONSTEXPR(x) _Generic((x), \
    s32: __builtin_ctz, u32: __builtin_ctz, \
    s64: __builtin_ctzll, u64: __builtin_ctzll)((x))
#define CLZ_CONSTEXPR(x) _Generic((x), \
    s32: __builtin_clz, u32: __builtin_clz, \
    s64: __builtin_clzll, u64: __builtin_clzll)((x))
#define POPCNT_CONSTEXPR(x) _Generic((x), \
    s32: __builtin_popcount, u32: __builtin_popcount, \
    s64: __builtin_popcountll, u64: __builtin_popcountll)((x))

#define MEMORY(name, size) \
union { \
    u8  b##8 [size/sizeof(u8 )]; \
    u16 b##16[size/sizeof(u16)]; \
    u32 b##32[size/sizeof(u32)]; \
} name;

#define MemoryRead(accesssize, memory, addr, memsize) \
    (((accesssize) == 32)   ? (memory.b##32)[(((addr) & ((memsize)-1))/sizeof(u32))] \
    : (((accesssize) == 16) ? (memory.b##16)[(((addr) & ((memsize)-1))/sizeof(u16))] \
                            : (memory.b##8) [(((addr) & ((memsize)-1))/sizeof(u8 ))]))

#define MemoryWrite(accesssize, memory, addr, memsize, write, mask) \
    (((accesssize) == 32)   ? ((memory.b##32)[(((addr) & ((memsize)-1))/sizeof(u32))] = (((memory.b##32)[(((addr) & ((memsize)-1))/sizeof(u32))] & ~(mask)) | ((write) & (mask)))) \
    : (((accesssize) == 16) ? ((memory.b##16)[(((addr) & ((memsize)-1))/sizeof(u16))] = (((memory.b##16)[(((addr) & ((memsize)-1))/sizeof(u16))] & ~(mask)) | ((write) & (mask)))) \
                            : ((memory.b##8) [(((addr) & ((memsize)-1))/sizeof(u8) )] = (((memory.b##8) [(((addr) & ((memsize)-1))/sizeof(u8) )] & ~(mask)) | ((write) & (mask))))))

#define DS_SWAP(l, r) \
{ typeof(l) tmp = (l); (l) = (r); (r) = tmp; }

#define DS_CLAMP(l, op, r) \
{ if ((l) op (r)) \
    (l) = (r); }

enum CPU_IDs : u8
{
    ARM7ID,
    ARM9ID,
    ARM11ID,
};

struct Pattern
{
    u32 cmp;
    u32 mask;
};

enum LoggingLevels : u64
{
    LOG_ALWAYS  = (0    ), // Always logged; used for emulator error logging.
    LOG_ARM7    = (1<<0 ), // Things under ownership of the ARM7TDMI.
    LOG_ARM9    = (1<<1 ), // Things under ownership of the ARM946E-S.
    LOG_ARM11   = (1<<2 ), // Things under ownership of the ARM11MPCore.
    LOG_UNIMP   = (1<<3 ), // For anything currently known to be unimplemented in the emulator.
    LOG_ODD     = (1<<4 ), // Program doing something weird that isn't inherently bad...?
    LOG_EXCEP   = (1<<5 ), // Hardware error handler has been triggered. Probably means the software has crashed.
    LOG_BUG     = (1<<6 ), // Program is triggering hardware bugs.
    LOG_VRAM    = (1<<7 ), // VRAM.
    LOG_PPU     = (1<<8 ), // PPU.
    LOG_FLASH   = (1<<9 ), // Flash.
    LOG_IO      = (1<<10), // Memory mapped IO.
    LOG_CARD    = (1<<11), // Gamecard.
    LOG_GX      = (1<<12), // 3D Geometry Engine.
    LOG_RTC     = (1<<13), // Real Time Clock.
    LOG_DMA     = (1<<14), // Direct Memory Access.
    LOG_WIFI    = (1<<15), // WiFi.
    LOG_TSC     = (1<<16), // Touch Screen Controller.
    LOG_SOUND   = (1<<17), // Sound Processing.
};

#define LOG_CPUID (1 << cpu->CPUID)
#define CPUIDtoCPUNum ((cpu->CPUID*2)+7)

#define MaskedWrite(dest, write, mask) ((dest) = (((dest) & ~(mask)) | ((write) & (mask))))

[[nodiscard]] static inline bool PatternMatch(const struct Pattern pattern, const u32 bits)
{
    return ((bits & pattern.mask) == pattern.cmp);
}

// for some reason there isn't a rotate right function i can use...?
[[nodiscard]] static inline u32 ROR32(const u32 val, u8 ror)
{
    // AND to hopefully avoid undefined behavior.
    ror &= 0x1F;
    return (val >> ror) | (val << ((32-ror) & 0x1F));
}

[[nodiscard]] static inline u32 ROL32(const u32 val, u8 rol)
{
    // AND to hopefully avoid undefined behavior.
    rol &= 0x1F;
    return (val << rol) | (val >> ((32-rol) & 0x1F));
}

// TODO: should this be per thread?
extern u64 LogMask;
// printf but with support for filtering out the noise
void LogPrint(const u64 logtype, const char* str, ...) __attribute__ ((format (printf, 2, 3)));
// logprint but with more crashing to desktop
[[noreturn]] void CrashSpectacularly(const char* str, ...) __attribute__ ((format (printf, 1, 2)));

// input handlers

u16 Input_PollExtra(const bool touched, void* pad);
u16 Input_PollMain(void* pad);

// runtime configuration data for the emulation core

typedef enum : u8
{
    NTRAudioOut_10,
    NTRAudioOut_16,

    NTRAudioOut_MAX [[maybe_unused]],
} NTRAudioOut;

typedef enum : u8
{
    NTRPowMan_NTR, // Phat
    NTRPowMan_USG, // Lite
    NTRPowMan_TWL, // DSi

    NTRPowMan_MAX [[maybe_unused]],
} NTRPowMan;

typedef enum : u8
{
    NTRFCRAM_4MiB, // NTR/USG Retail
    NTRFCRAM_8MiB, // NTR/USG Debugger
    NTRFCRAM_16MiB, // TWL Retail
    NTRFCRAM_32MiB, // TWL Debugger/3DS Retail

    NTRFCRAM_MAX [[maybe_unused]],
} NTRFCRAM; // TODO: replace with selection of specific chips?

typedef enum : u8
{
    WiFiNVRAM_256KiB, // NTR/USG
    WiFiNVRAM_512KiB, // iQue DS // codename?
    WiFiNVRAM_128KiB, // Early TWL?
    WiFiNVRAM_4KiB, // Late TWL/All 3DS?

    WiFiNVRAM_MAX [[maybe_unused]],
} WiFiNVRAMSize; // TODO: replace with selection of specific chips?

typedef enum : u8
{
    WiFiNVRAMWriteProt_Enabled,
    WiFiNVRAMWriteProt_Disabled,

    WiFiNVRAMWriteProt_MAX [[maybe_unused]],
} WiFiNVRAMWriteProt; // can be disabled on some retail models by shorting a pin iirc?

typedef enum : u8
{
    ConsoleModel_Custom,

    // TODO: dev models?
    ConsoleModel_NTR001, // DS (Phat)
    // TODO: late NTR models using a ds lite soc apparently existed?
    ConsoleModel_USG001, // DS Lite

    ConsoleModel_TWL001, // DSi
    ConsoleModel_UTL001, // DSi XL

    ConsoleModel_CTR001, // (old) 3DS
    ConsoleModel_SPR001, // (old) 3DS XL
    ConsoleModel_FTR001, // (old) 2DS
    ConsoleModel_KTR001, // New 3DS
    ConsoleModel_RED001, // New 3DS XL
    ConsoleModel_JAN001, // New 2DS XL

    ConsoleModel_MAX [[maybe_unused]],
} ConsoleModel;

// cfg used at runtime to determine what model the core should emulate for each component
typedef struct
{
    NTRAudioOut NTRAudioOut;
    NTRPowMan NTRPowMan;
    NTRFCRAM NTRFCRAM;
    WiFiNVRAMSize WiFiNVRAMSize;
    WiFiNVRAMWriteProt WiFiNVRAMWriteProt;
    u16 TSCL;
    u16 TSCR;
    u16 TSCT;
    u16 TSCB;
} SysCfg;

// cfg used to initialize the emulator core
typedef struct
{
    SysCfg SysCfg;
    ConsoleModel Model;
    char* CustomModel;
    struct {
        char* Bios7;
        char* PakROM;
        char* PakSRAM;
    } AGB; // AGB+ (excl. TWL)
    struct {
        char* Bios7;
        char* Bios9;
        char* NVRAM;
        char* CardROM;
        char* CardSRAM;
    } NTR; // NTR+
    struct {
        char* Bios7;
        char* Bios9;
        char* NAND;
        char* SDCard;
    } TWL;// TWL+
    struct {
        char* Boot9;
        char* Boot11;
    } CTR; // CTR+
} CoreCfg;

// coroutine stuff
#ifdef REALTHREAD
extern volatile bool CR_Kill;
#else
constexpr bool CR_Kill = false;
#endif
extern volatile bool CR_Start;

bool CR_Create(coroutine* handle, void (*func)(void*), void* param);
void CR_Free(coroutine handle);
void CR_Switch(coroutine handle);
coroutine CR_Active();
