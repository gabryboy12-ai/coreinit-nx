#pragma once
#include <stdint.h>
#include "coreinit/mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

// DA VERIFICARE in wut/include/coreinit/condition.h:
//   WUT_CHECK_SIZE(OSCondition,
#define COREINIT_NX_OSCONDITION_SIZE 0x2C

typedef struct OSCondition {
    uint8_t _opaque[COREINIT_NX_OSCONDITION_SIZE];
} OSCondition;

void OSInitCond(OSCondition *cond);
void OSInitCondEx(OSCondition *cond, const char *name);
void OSWaitCond(OSCondition *cond, OSMutex *mutex);
void OSSignalCond(OSCondition *cond);

#ifdef __cplusplus
}
#endif