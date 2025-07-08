#pragma once

#include <stddef.h>
#include "../../utils.h"
#include "../arm_shared/arm.h"



// Full Model Name: ARM946E-S r1p1

/*  misc notes:
    runs at 4x base clock on NDS
    (optionally) 8x base clock on DSi/3DS
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

struct ARM946E_S
{
    struct ARM ARM;
    alignas(HOST_CACHEALIGN) u64 RegIL[16][2]; // r15 shouldn't be able to interlock?
    bool BoostedClock; /*   Determines whether the ARM9 is running at 4 or 2 times the bus clock.
                        *   Should only apply to the DSi bus.
                        *   true  = 4x
                        *   false = 2x.
                        *   Checkme: Is it faster to do this branchless?
                        *   Checkme: Can 3DS get an 8x or 1x clock multiplier with some jank?
                        */
};

// ensure casting between the two types works as expected
static_assert(offsetof(struct ARM946E_S, ARM) == 0);

// reset vector exception
void ARM9_Reset(struct ARM946E_S* ARM9);
// run to initialize the cpu; assumes everything was zero'd out; should be akin to a cold boot
void ARM9_Init(struct ARM946E_S* ARM9);
// cycledelay: time between the instruction beginning and register being fetched; used for interlock handling
// readportc: refers to the port used to read from the register bank, does not allow for forwarding from certain instructions resulting in different interlock conditions
u32 ARM9_GetReg(struct ARM946E_S* ARM9, const int reg, const int cycledelay, const bool readportc);

void ARM9_AddCycles(struct ARM946E_S* ARM9, const int Execute, const int Memory);
