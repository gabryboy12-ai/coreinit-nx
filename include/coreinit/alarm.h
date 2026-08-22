#pragma once
#include <stdint.h>
#include "coreinit/time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OSAlarm      { uint8_t _opaque[0x58]; } OSAlarm;
typedef struct OSAlarmQueue { uint8_t _opaque[0x24]; } OSAlarmQueue;
typedef struct OSContext    OSContext;

typedef void (*OSAlarmCallback)(OSAlarm *, OSContext *);

void    OSCreateAlarm(OSAlarm *alarm);
void    OSCreateAlarmEx(OSAlarm *alarm, const char *name);
int32_t OSSetAlarm(OSAlarm *alarm, OSTime time, OSAlarmCallback callback);
int32_t OSSetPeriodicAlarm(OSAlarm *alarm, OSTime start, OSTime interval,
                           OSAlarmCallback callback);
int32_t OSCancelAlarm(OSAlarm *alarm);
void    OSCancelAlarms(uint32_t group);
void    OSSetAlarmTag(OSAlarm *alarm, uint32_t group);
void    OSSetAlarmUserData(OSAlarm *alarm, void *data);
void   *OSGetAlarmUserData(OSAlarm *alarm);
int32_t OSWaitAlarm(OSAlarm *alarm);
// --- estensione coreinit-nx, non parte di Cafe OS ---
// Ferma il thread scheduler degli allarmi. Va chiamata prima che il
// processo termini: senza, il thread sopravvive alla distruzione dello
// stato statico su cui dorme.
void coreinitNxAlarmShutdown(void);

#ifdef __cplusplus
}
#endif