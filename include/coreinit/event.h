#pragma once
#include <stdint.h>
#include "coreinit/time.h"

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: WUT_CHECK_SIZE(OSEvent, 0x24)
typedef struct OSEvent { uint8_t _opaque[0x24]; } OSEvent;

typedef enum OSEventMode {
    OS_EVENT_MODE_MANUAL = 0,
    OS_EVENT_MODE_AUTO   = 1,
} OSEventMode;

void OSInitEvent(OSEvent *event, int32_t value, OSEventMode mode);
void OSInitEventEx(OSEvent *event, int32_t value, OSEventMode mode,const char *name);
void    OSSignalEvent(OSEvent *event);
void    OSSignalEventAll(OSEvent *event);
void    OSWaitEvent(OSEvent *event);
void    OSResetEvent(OSEvent *event);
int32_t OSWaitEventWithTimeout(OSEvent *event, OSTime timeout);

#ifdef __cplusplus
}
#endif