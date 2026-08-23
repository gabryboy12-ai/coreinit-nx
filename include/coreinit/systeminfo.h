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

uint64_t OSGetTitleID(void);
int32_t  OSEnableHomeButtonMenu(int32_t enable);
int32_t  OSIsHomeButtonMenuEnabled(void);
int32_t  ENVGetEnvironmentVariable(const char *name, char *buffer,
                                   uint32_t length);

#ifdef __cplusplus
}
#endif