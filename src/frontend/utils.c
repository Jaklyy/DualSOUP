#include <stdio.h>
#include <stdarg.h>
#include "../core/utils.h"
#include "../../libs/libco/libco.h"
#include <stdlib.h>




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

void CrashSpectacularly(const char* str, ...)
{
    va_list args;
    va_start(args);

    vprintf(str, args);

    va_end(args);

    exit(EXIT_FAILURE);
}

// coroutine junk

// internal variable used to pass along parameters to the new cothread
thread_local void* pass[3];

// internal coroutine startup handler
void CR_Boot()
{
    // im not sure if the compiler is forced to save these...?
    void* func = pass[0];
    void* param = pass[1];

    // we only wanted to initalize the thread we dont need to run it *yet*
    co_switch(pass[2]);

    // jump to saved function using saved parameter
    ((void (*)(void*))func)((void*)param);
}

cothread_t CR_Create(void (*func)(void*), void* param)
{
    // 2 MiB is probably way more than needed but idc
    cothread_t cr = co_create(((MiB(2)/8)*sizeof(void*)), CR_Boot);
    pass[0] = func;
    pass[1] = param;
    pass[2] = co_active();
    co_switch(cr);
    return cr;
}

void CR_Free(cothread_t handle)
{
    co_delete(handle);
}

void CR_Switch(cothread_t handle)
{
    co_switch(handle);
}
