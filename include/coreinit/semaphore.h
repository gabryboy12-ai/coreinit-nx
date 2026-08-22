#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: WUT_CHECK_SIZE(OSSemaphore, 0x20)
typedef struct OSSemaphore { uint8_t _opaque[0x20]; } OSSemaphore;

void    OSInitSemaphore(OSSemaphore *semaphore, int32_t count);
void    OSInitSemaphoreEx(OSSemaphore *semaphore, int32_t count,
                          const char *name);
int32_t OSGetSemaphoreCount(OSSemaphore *semaphore);
int32_t OSSignalSemaphore(OSSemaphore *semaphore);
int32_t OSWaitSemaphore(OSSemaphore *semaphore);
int32_t OSTryWaitSemaphore(OSSemaphore *semaphore);

#ifdef __cplusplus
}
#endif