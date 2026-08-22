#pragma once
#include <stdint.h>
#include "coreinit/time.h"

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: WUT_CHECK_SIZE(OSSpinLock, 0x10)
typedef struct OSSpinLock { uint8_t _opaque[0x10]; } OSSpinLock;

void    OSInitSpinLock(OSSpinLock *spinlock);
int32_t OSAcquireSpinLock(OSSpinLock *spinlock);
int32_t OSTryAcquireSpinLock(OSSpinLock *spinlock);
int32_t OSTryAcquireSpinLockWithTimeout(OSSpinLock *spinlock, OSTime timeout);
int32_t OSReleaseSpinLock(OSSpinLock *spinlock);
int32_t OSUninterruptibleSpinLock_Acquire(OSSpinLock *spinlock);
int32_t OSUninterruptibleSpinLock_TryAcquire(OSSpinLock *spinlock);
int32_t OSUninterruptibleSpinLock_TryAcquireWithTimeout(OSSpinLock *spinlock,
                                                        OSTime timeout);
int32_t OSUninterruptibleSpinLock_Release(OSSpinLock *spinlock);

#ifdef __cplusplus
}
#endif