#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t  LCHardwareIsAvailable(void);
void    *LCAlloc(uint32_t size);
void     LCDealloc(void *addr);
uint32_t LCGetMaxSize(void);
uint32_t LCGetAllocatableSize(void);
uint32_t LCGetUnallocated(void);
int32_t  LCIsDMAEnabled(void);
int32_t  LCEnableDMA(void);
void     LCDisableDMA(void);
uint32_t LCGetDMAQueueLength(void);
void     LCLoadDMABlocks(void *dst, const void *src, uint32_t size);
void     LCStoreDMABlocks(void *dst, const void *src, uint32_t size);
void     LCWaitDMAQueue(uint32_t queueLength);

#ifdef __cplusplus
}
#endif