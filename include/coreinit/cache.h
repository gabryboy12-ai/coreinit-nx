#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void DCFlushRange(void *addr, uint32_t size);
void DCStoreRange(void *addr, uint32_t size);

#ifdef __cplusplus
}
#endif