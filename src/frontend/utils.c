#include <stdio.h>
#include <stdarg.h>
#include "../core/utils.h"




u64 LogMask;

void LogPrint(const u64 logtype, const char* str, ...)
{
#ifndef NOLOGGING
    if ((LogMask & logtype) != logtype) return;

    va_list args;
    va_start(args);

    vprintf(str, args);

    va_end(args);
#endif
}