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
typedef void (*OSThreadCleanupCallbackFn)(OSThread *thread, void *stack);
typedef enum OSThreadSpecificID {
    OS_THREAD_SPECIFIC_0 = 0,
    // ... fino a 13; 14 e 15 sono riservati da wut
} OSThreadSpecificID;

// DA VERIFICARE la firma esatta in wut/include/coreinit/thread.h
typedef void (*OSThreadDeallocatorFn)(OSThread *thread, void *stack);

uint32_t    OSGetThreadAffinity(OSThread *thread);
int32_t     OSGetThreadPriority(OSThread *thread);
const char *OSGetThreadName(OSThread *thread);
int32_t     OSSetThreadAffinity(OSThread *thread, uint32_t affinity);
int32_t     OSSetThreadPriority(OSThread *thread, int32_t priority);
void        OSSetThreadName(OSThread *thread, const char *name);

void  *OSGetThreadSpecific(OSThreadSpecificID id);
void   OSSetThreadSpecific(OSThreadSpecificID id, void *value);

OSThreadDeallocatorFn OSSetThreadDeallocator(OSThread *thread, OSThreadDeallocatorFn fn);

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
OSThread *OSGetCurrentThread(void);
uint32_t  OSGetCoreId(void); // TODO: spostare in coreinit/core.h
void     OSCancelThread(OSThread *thread);
void     OSDetachThread(OSThread *thread);
void     OSTestThreadCancel(void);
int32_t  OSSetThreadCancelState(int32_t state);
uint32_t OSGetStackPointer(void);
void     OSBlockThreadsOnExit(void);
int32_t OSIsThreadTerminated(OSThread *thread);

OSThreadCleanupCallbackFn OSSetThreadCleanupCallback(
        OSThread *thread, OSThreadCleanupCallbackFn callback);

#ifdef __cplusplus
}
#endif