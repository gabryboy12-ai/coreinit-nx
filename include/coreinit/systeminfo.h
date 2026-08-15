#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: WUT_CHECK_SIZE(OSSystemInfo, 0x20)
typedef struct OSSystemInfo {
    uint32_t busClockSpeed;
    uint32_t coreClockSpeed;
    int64_t  baseTime;
    uint8_t  _unknown[0x10];
} OSSystemInfo;

OSSystemInfo *OSGetSystemInfo(void);

#ifdef __cplusplus
}
#endif