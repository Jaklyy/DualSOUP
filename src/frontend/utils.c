#include <stdio.h>
#include <stdarg.h>
#include "core/utils.h"




u64 LogMask;
char* DualSOUPFolder;

void LogPrint(const u64 logtype, const char* str, ...)
{
#ifndef NOLOGGING
    if ((LogMask & logtype) != logtype) return;
#else
    if (logtype != LOG_ALWAYS) return;
#endif

    va_list args;
    va_start(args, str);

    vprintf(str, args);

    va_end(args);
}

[[noreturn]] void CrashSpectacularly(const char* str, ...)
{
    va_list args;
    va_start(args, str);

    vprintf(str, args);

    va_end(args);

    __builtin_trap();
    //exit(EXIT_FAILURE);
}
