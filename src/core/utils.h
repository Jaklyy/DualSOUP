#pragma once

#include <stdint.h>
#include <stddef.h>





typedef int8_t s8;
typedef uint8_t u8;

typedef int16_t s16;
typedef uint16_t u16;

typedef int32_t s32;
typedef uint32_t u32;

typedef int64_t s64;
typedef uint64_t u64;
typedef uint64_t timestamp;


// todo: actually add fallback paths if these dont exist for w/e reason
#define likely(x) __builtin_expect(!!x, 1)
#define unlikely(x) __builtin_expect(!!x, 0)
#define bswap(x) _Generic((x), \
       u16: __builtin_bswap16, \
       u32: __builtin_bswap32)((x))

#define HOST_CACHEALIGN (64)

enum CPU_IDs : u8
{
    ARM7ID,
    ARM9ID,
    ARM11ID,
};

struct Pattern
{
    u32 bits;
    u32 mask;
};

enum LoggingLevels : u64
{
    LOG_ARM7    = (1<<0 ), // Things under ownership of the ARM7TDMI
    LOG_ARM9    = (1<<1 ), // Things under ownership of the ARM946E-S
    LOG_ARM11   = (1<<2 ), // Things under ownership of the ARM11MPCore
    LOG_UNIMP   = (1<<3 ), // For anything currently known to be unimplemented in the emulator.
    LOG_ODD     = (1<<4 ), // Program doing something weird that isn't inherently bad...?
};

extern u64 LogMask;

inline bool PatternMatch(const struct Pattern pattern, const u32 bits)
{
    return ((bits & pattern.mask) == pattern.bits);
}

inline u32 ROR32(u64 val, u8 ror) // u64 solely to make ub-san shutup!!
{
    return (val >> ror) | (val << (32-ror));
}

// printf but with support for filtering out the noise
void LogPrint(const u64 logtype, const char* str, ...);

// in theory we could use smaller timestamps and use ckd_add and chk_sub to adjust all the timestamps when one overflows.
// this might incur a noticeable performance penalty on every single timestamp increment.
// but it would allow for the system to be infinitely running (mind you 727 years is probably a long enough time already)
// it may also allow for faster scheduling by being able to pack more scheduler timestamps into a single simd reg?
timestamp IncrementTimestamp(timestamp Timestamp, u64 increment)
{
    return Timestamp + increment;
}
