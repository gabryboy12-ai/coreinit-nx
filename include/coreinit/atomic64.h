#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t OSGetAtomic64(volatile uint64_t *ptr);
uint64_t OSSetAtomic64(volatile uint64_t *ptr, uint64_t value);
int32_t  OSCompareAndSwapAtomic64(volatile uint64_t *ptr, uint64_t compare,uint64_t value);
uint64_t OSSwapAtomic64(volatile uint64_t *ptr, uint64_t value);
int64_t  OSAddAtomic64(volatile int64_t *ptr, int64_t value);
uint64_t OSAndAtomic64(volatile uint64_t *ptr, uint64_t value);
uint64_t OSOrAtomic64(volatile uint64_t *ptr, uint64_t value);
uint64_t OSXorAtomic64(volatile uint64_t *ptr, uint64_t value);
int32_t  OSTestAndClearAtomic64(volatile uint64_t *ptr, uint32_t bit);
int32_t  OSTestAndSetAtomic64(volatile uint64_t *ptr, uint32_t bit);
void OSMemoryBarrier(void);

#ifdef __cplusplus
}
#endif