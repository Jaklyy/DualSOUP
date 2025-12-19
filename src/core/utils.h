#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbit.h>
#include "../frontend/coroutine.h"
#include <stdio.h>




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
#ifdef UseThreads
typedef volatile uint64_t timestamp;
#else
typedef uint64_t timestamp;
#endif
#define timestamp_max (UINT64_MAX)

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)
#define GiB(x) ((x) * 1024 * 1024 * 1024)


// todo: actually add fallback paths if these dont exist for w/e reason
/*#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)*/
#define bswap(x) _Generic((x), \
    s16: __builtin_bswap16, u16: __builtin_bswap16, \
    s32: __builtin_bswap32, u32: __builtin_bswap32, \
    s64: __builtin_bswap64, u64: __builtin_bswap64)((x))

#define HOST_CACHEALIGN (64)


// the builtins are constexpr but the actual standard defined functions aren't...
#define CTZ_CONSTEXPR(x) _Generic((x), \
    s32: __builtin_ctz, u32: __builtin_ctz, \
    s64: __builtin_ctzl, u64: __builtin_ctzl)((x))
#define CLZ_CONSTEXPR(x) _Generic((x), \
    s32: __builtin_clz, u32: __builtin_clz, \
    s64: __builtin_clzl, u64: __builtin_clzl)((x))
#define POPCNT_CONSTEXPR(x) _Generic((x), \
    s32: __builtin_popcount, u32: __builtin_popcount, \
    s64: __builtin_popcountl, u64: __builtin_popcountl)((x))

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
{ typeof(l) tmp = l; l = r; r = tmp; }

#define DS_CLAMP(l, op, r) \
if (l op r) \
    l = r;

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
    LOG_VRAM    = (1<<7 ), // vram
    LOG_PPU     = (1<<8 ), // ppu stuff
    LOG_FLASH   = (1<<9 ), // flash
    LOG_IO      = (1<<10), // ahb mmapped io
    LOG_CARD    = (1<<11), // gamecard
    LOG_GX      = (1<<12), // 3d geometry engine
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
    ror &= 0x1F; // do this to hopefully avoid undefined behavior.
    return (val >> ror) | (val << (32-ror));
}

[[nodiscard]] static inline u32 ROL32(const u32 val, u8 rol)
{
    rol &= 0x1F; // do this to hopefully avoid undefined behavior.
    return (val << rol) | (val >> (32-rol));
}

// TODO: should this be per thread?
extern u64 LogMask;
// printf but with support for filtering out the noise
void LogPrint(const u64 logtype, const char* str, ...) __attribute__ ((format (printf, 2, 3)));
// logprint but with more crashing to desktop
void CrashSpectacularly(const char* str, ...) __attribute__ ((format (printf, 1, 2)));

// coroutine stuff
#ifdef UseThreads
extern volatile bool CR_Kill;
#else
constexpr bool CR_Kill = false;
#endif
extern volatile bool CR_Start;

bool CR_Create(coroutine* handle, void (*func)(void*), void* param);
void CR_Free(coroutine handle);
void CR_Switch(coroutine handle);
coroutine CR_Active();

u16 Input_PollExtra(void* pad);
u16 Input_PollMain(void* pad);
