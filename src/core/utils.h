#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>

#include <SDL3/SDL_mutex.h>

#include "frontend/coroutine.h"




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
    static unsigned int ds_internal_stdctzll [[maybe_unused]] (unsigned long long input)
    {
        return __builtin_ctzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdctzl [[maybe_unused]] (unsigned long input)
    {
        return __builtin_ctzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdctzi [[maybe_unused]] (unsigned int input)
    {
        return __builtin_ctzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdctzs [[maybe_unused]] (unsigned short input)
    {
        return __builtin_ctzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdctzc [[maybe_unused]] (unsigned char input)
    {
        return __builtin_ctzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    #define stdc_trailing_zeros(x) _Generic((x), \
        char: ds_internal_stdctzc, unsigned char: ds_internal_stdctzc, \
        short: ds_internal_stdctzs, unsigned short: ds_internal_stdctzs, \
        int: ds_internal_stdctzi, unsigned: ds_internal_stdctzi, \
        long: ds_internal_stdctzl, unsigned long: ds_internal_stdctzl, \
        long long: ds_internal_stdctzll, unsigned long long: ds_internal_stdctzll)((x))
#endif

#ifndef stdc_trailing_ones
    //#warning "stdc_trailing_ones not found, using fallback."
    #define stdc_trailing_ones(x) stdc_trailing_zeros((typeof(x))~(x))
#endif

#ifndef stdc_leading_zeros
    //#warning "stdc_leading_zeros not found, using fallback."
    static unsigned int ds_internal_stdclzll [[maybe_unused]] (unsigned long long input)
    {
        return __builtin_clzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdclzl [[maybe_unused]] (unsigned long input)
    {
        return __builtin_clzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdclzi [[maybe_unused]] (unsigned int input)
    {
        return __builtin_clzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdclzs [[maybe_unused]] (unsigned short input)
    {
        return __builtin_clzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    static unsigned int ds_internal_stdclzc [[maybe_unused]] (unsigned char input)
    {
        return __builtin_clzg(input, (int)(sizeof(input) * CHAR_BIT));
    }
    #define stdc_leading_zeros(x) _Generic((x), \
        char: ds_internal_stdclzc, unsigned char: ds_internal_stdclzc, \
        short: ds_internal_stdclzs, unsigned short: ds_internal_stdclzs, \
        int: ds_internal_stdclzi, unsigned: ds_internal_stdclzi, \
        long: ds_internal_stdclzl, unsigned long: ds_internal_stdclzl, \
        long long: ds_internal_stdclzll, unsigned long long: ds_internal_stdclzll)((x))
#endif

#ifndef stdc_leading_ones
    //#warning "stdc_leading_ones not found, using fallback."
    #define stdc_leading_ones(x) stdc_leading_zeros((typeof(x))~(x))
#endif

#ifndef stdc_count_ones
    //#warning "stdc_count_ones not found, using fallback."
    #define stdc_count_ones(x) __builtin_popcountll((long long)(x))
#endif

#ifndef stdc_count_zeros
    //#warning "stdc_count_zeros not found, using fallback."
    #define stdc_count_zeros(x) stdc_count_ones((typeof(x))~(x))
#endif

// C29; may break
#if __has_include(<stdcountof.h>)
    #include <stdcountof.h>
#endif
#ifndef countof
    #define countof(x) _Countof(x)
#endif

// C29; may break
#if __has_include(<stddefer.h>)
    #include <stddefer.h>
#endif
    #ifndef defer
    #define __DEFER__(F, V)            \
    auto void F(int*);                 \
    __attribute__((cleanup(F))) int V; \
    __attribute__((always_inline))     \
    auto inline void F(int*)

    #define defer __DEFER(__COUNTER__)
    #define __DEFER(N) __DEFER_(N)
    #define __DEFER_(N) __DEFER__(__DEFER_FUNCTION_ ## N, __DEFER_VARIABLE_ ## N)
#endif

#define bswap(x) _Generic((x), \
    s16: __builtin_bswap16, u16: __builtin_bswap16, \
    s32: __builtin_bswap32, u32: __builtin_bswap32, \
    s64: __builtin_bswap64, u64: __builtin_bswap64)((x))

#define HOST_CACHEALIGN (64)

#define forceinline __attribute((always_inline)) inline


// the builtins are constexpr but the actual standard defined functions aren't...
#define CTZ_CONSTEXPR(x) (((x) == 0) ? (sizeof((x)) * CHAR_BIT) : (_Generic((x), \
    /*char: __builtin_ctz, unsigned char: __builtin_ctz, \
    short: __builtin_ctz, unsigned short: __builtin_ctz,*/ \
    int: __builtin_ctz, unsigned: __builtin_ctz, \
    long: __builtin_ctzl, unsigned long: __builtin_ctzl, \
    long long: __builtin_ctzll, unsigned long long: __builtin_ctzll)((x))))
#define CTO_CONSTEXPR(x) ((~(x) == 0) ? (sizeof((x)) * CHAR_BIT) : (_Generic((x), \
    /*char: __builtin_ctz, unsigned char: __builtin_ctz, \
    short: __builtin_ctz, unsigned short: __builtin_ctz,*/ \
    int: __builtin_ctz, unsigned: __builtin_ctz, \
    long: __builtin_ctzl, unsigned long: __builtin_ctzl, \
    long long: __builtin_ctzll, unsigned long long: __builtin_ctzll)((~(x)))))

#define CLZ_CONSTEXPR(x) (((x) == 0) ? (sizeof((x)) * CHAR_BIT) : (_Generic((x), \
    /*char: __builtin_clz, unsigned char: __builtin_clz, \
    short: __builtin_clz, unsigned short: __builtin_clz,*/ \
    int: __builtin_clz, unsigned: __builtin_clz, \
    long: __builtin_clzl, unsigned long: __builtin_clzl, \
    long long: __builtin_clzll, unsigned long long: __builtin_clzll)((x)))) \

#define CLO_CONSTEXPR(x) ((~(x) == 0) ? (sizeof((x)) * CHAR_BIT) : (_Generic((x), \
    /*char: __builtin_clz, unsigned char: __builtin_clz, \
    short: __builtin_clz, unsigned short: __builtin_clz,*/ \
    int: __builtin_clz, unsigned: __builtin_clz, \
    long: __builtin_clzl, unsigned long: __builtin_clzl, \
    long long: __builtin_clzll, unsigned long long: __builtin_clzll)(~(x))))

#define POPCNT_CONSTEXPR(x) _Generic((x), \
    int: __builtin_popcount, unsigned: __builtin_popcount, \
    long: __builtin_popcountl, unsigned long: __builtin_popcountl, \
    long long: __builtin_popcountll, unsigned long long: __builtin_popcountll)((x))

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

#define MaskedWrite(dest, write, mask) ((dest) = (((dest) & ~(mask)) | ((write) & (mask))))

#define DS_SWAP(l, r) \
{ typeof(l) tmp = (l); (l) = (r); (r) = tmp; }

#define DS_CLAMP(l, op, r) \
{ if ((l) op (r)) \
    (l) = (r); }

typedef enum : u8
{
    ARM7ID,
    ARM9ID,
    //ARM11ID,
} CPU_IDs;

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
    LOG_CARD    = (1<<11), // Game Card.
    LOG_GX      = (1<<12), // 3D Geometry Engine.
    LOG_RTC     = (1<<13), // Real Time Clock.
    LOG_DMA     = (1<<14), // Direct Memory Access.
    LOG_WIFI    = (1<<15), // WiFi.
    LOG_TSC     = (1<<16), // Touch Screen Controller.
    LOG_SOUND   = (1<<17), // Sound Processing.
    LOG_PAK     = (1<<18), // Game Pak.
    LOG_FCRAM   = (1<<19), // FCRAM (aka Main RAM).
};

#define LOG_CPUID (1 << cpu->CPUID)
#define CPUIDtoCPUNum ((cpu->CPUID*2)+7)

struct Pattern
{
    u32 cmp;
    u32 mask;
};

[[nodiscard]] static inline bool PatternMatch(const struct Pattern pattern, const u32 bits)
{
    return ((bits & pattern.mask) == pattern.cmp);
}

// for some reason there isn't a rotate right function i can use...?
[[nodiscard]] static inline u32 ROR32(const u32 val, u8 ror)
{
    ror &= 0x1F;
    return (val >> ror) | (val << ((32-ror) & 0x1F));
}

[[nodiscard]] static inline u32 ROL32(const u32 val, u8 rol)
{
    rol &= 0x1F;
    return (val << rol) | (val >> ((32-rol) & 0x1F));
}

[[nodiscard]] static inline u32 MakeWriteMask(u32 addr, u8 size)
{
    // TODO: test if one of these approaches is actually meaningfully faster
#if 0
    const u32 width = 8<<size;
    const u32 mask = ROL32(((s64)-0x100000000 >> width), ((addr & 0x3) * 8) + 1);
    return mask;
#else
    switch(size)
    {
        case 0: return 0xFF   << (addr & 3) * 8;
        case 1: return 0xFFFF << (addr & 3) * 8;
        case 2: return 0xFFFFFFFF;
        default: unreachable();
    }
#endif
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
    NTRPMIC_NTR, // Phat
    NTRPMIC_USG, // Lite
    NTRPMIC_TWL, // DSi

    NTRPMIC_MAX [[maybe_unused]],
} NTRPMIC;

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
    NTRPMIC NTRPMIC;
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
