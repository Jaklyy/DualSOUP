#pragma once

#include <stdint.h>
#include <stddef.h>





typedef int8_t s8;
typedef uint8_t u8;
typedef uint8_t byte;

typedef int16_t s16;
typedef uint16_t u16;
typedef uint16_t halfword;

typedef int32_t s32;
typedef uint32_t u32;
typedef uint32_t word;

typedef int64_t s64;
typedef uint64_t u64;
typedef uint64_t doubleword;


// todo: actually add fallback paths if these dont exist for w/e reason
#define likely(x) __builtin_expect(!!x, 1)
#define unlikely(x) __builtin_expect(!!x, 0)
#define bswap(x) _Generic((x), \
       u16: __builtin_bswap16, \
       u32: __builtin_bswap32)((x))

#define HOST_CACHEALIGN (64)

enum CPU_IDs : u8
{
    ARM7ID  = 0,
    ARM9ID  = 1,
    ARM11ID = 2,
};

struct Pattern
{
    u32 bits;
    u32 mask;
};

inline bool PatternMatch(const struct Pattern pattern, const u32 bits)
{
    return ((bits & pattern.mask) == pattern.bits);
}

inline u32 ROR32(u64 val, u8 ror) // u64 solely to make ub-san shutup!!
{
    return (val >> ror) | (val << (32-ror));
}
