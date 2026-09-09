



#ifdef REALTHREAD
#include <threads.h>
typedef thrd_t coroutine;
#else
#include "libco/libco.h"
typedef cothread_t coroutine;
#endif
