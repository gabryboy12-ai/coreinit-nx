#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: WUT_CHECK_SIZE(OSThread, 0x6a0)
#define COREINIT_NX_OSTHREAD_SIZE 0x6A0

typedef struct OSThread {
    uint8_t _opaque[COREINIT_NX_OSTHREAD_SIZE];
} OSThread;

typedef int (*OSThreadEntryPointFn)(int argc, const char **argv);
typedef uint8_t OSThreadAttributes;

enum {
    OS_THREAD_ATTRIB_AFFINITY_CPU0 = 1 << 0,
    OS_THREAD_ATTRIB_AFFINITY_CPU1 = 1 << 1,
    OS_THREAD_ATTRIB_AFFINITY_CPU2 = 1 << 2,
    OS_THREAD_ATTRIB_AFFINITY_ANY  = 0x7,
    OS_THREAD_ATTRIB_DETACHED      = 1 << 3,
    OS_THREAD_ATTRIB_STACK_USAGE   = 1 << 5,
};

int32_t OSCreateThread(OSThread *thread,
                       OSThreadEntryPointFn entry,
                       int32_t argc,
                       char *argv,
                       void *stack,
                       uint32_t stackSize,
                       int32_t priority,
                       OSThreadAttributes attributes);

int32_t OSResumeThread(OSThread *thread);
int32_t OSSuspendThread(OSThread *thread);
int32_t OSJoinThread(OSThread *thread, int *threadResult);
void    OSExitThread(int32_t result);

#ifdef __cplusplus
}
#endif