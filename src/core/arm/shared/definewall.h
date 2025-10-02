#if 0



// header for handling a bunch of shared crud used to init header files

#ifdef DEFINEWALL
    #undef DEFINEWALL
    #undef NAME
    #undef NAME2
    #undef NAME3
    #undef ARM_SetReg
    #undef ARM_GetReg
    #undef ADDCYCLES
    #undef CAST
#else

    #ifndef CPUNUM
        #error "CPUNUM NOT DEFINED!!!"
    #endif

    #if CPUNUM == 9
        #include "../arm9/arm.h"
    #elif CPUNUM == 7
        #include "../arm7/arm.h"
    #elif CPUNUM == 11
        #include "../arm11/arm.h"
    #else
        #error "THIS IS NOT A VALID CPUNUM!!! IM NOT EMULATING THAT!!!"
    #endif

    #define DEFINEWALL

    // yes we do need the nested macros to make it actually concatenate the macro's value instead of the macro's name
    #define NAME3(x, y, z) x##z##_##y
    #define NAME2(x, y, z) NAME3(x, y, z)
    #define NAME(x, y) NAME2(x, y, CPUNUM)
    #define ARM_SetReg NAME2(ARM, SetReg, CPUNUM)
    #define ARM_GetReg NAME2(ARM, GetReg, CPUNUM)
    #define ADDCYCLES NAME2(ARM, AddCycles, CPUNUM)

    #if CPUNUM == 9
        #define CAST ((struct ARM946ES *)cpu)
    #elif CPUNUM == 7
        #define CAST ((struct ARM7TDMI *) cpu)
    #elif CPUNUM == 11
        #define CAST ((struct ARM11MPCore *)cpu)
    #else
        #error "THIS IS NOT A VALID CPUNUM!!! IM NOT EMULATING THAT!!!"
    #endif
#endif
#endif 
