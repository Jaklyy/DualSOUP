#include <stdio.h>
#include "../utils.h"
#include "bus.h"




#define WIDTH 32

#ifndef WIDTH
    #error "BUS WIDTH NOT DEFINED!!!"
#endif

#if WIDTH == 32
    #define ALIGN 0x3
    #define PUN *(u32*) // This is illegal, you know.
    #define READRET16(v) (struct BusReadRet){v, {1, 1}}
#elif WIDTH == 16
    #define ALIGN 0x1
    #define PUN *(u16*)
    #define READRET16(v) (struct BusReadRet){v, {2, 2}}
#elif WIDTH == 8
    #define ALIGN 0x0
    #define PUN *(u8*)
    #define READRET16(v) (struct BusReadRet){v, {2, 2}}
#else
    #error "INVALID BUS WIDTH!!!"
#endif

// yes we do need the nested macros to make it actually concatenate the macro's value instead of the macro's name
#define NAME3(x, y) x##y
#define NAME2(x, y) NAME3(x, y)
#define NAME(x) NAME2(x, WIDTH)

#define READRET32(v) (struct BusReadRet){v, {1, 1}}


#undef ALIGN
#undef READRET32
#undef READRET16
#undef PUN
#undef NAME3
#undef NAME2
#undef NAME1
