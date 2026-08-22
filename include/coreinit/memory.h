#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *OSBlockMove(void *dst, const void *src, uint32_t size, int32_t flush);
void *OSBlockSet(void *dst, uint8_t val, uint32_t size);

#ifdef __cplusplus
}
#endif