



#ifdef UseThreads
#include <threads.h>
typedef thrd_t coroutine;
#else
#include "../../libs/libco/libco.h"
typedef cothread_t coroutine;
#endif