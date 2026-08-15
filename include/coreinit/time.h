#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t OSTick;
typedef int64_t OSTime;

// Da wut: WUT_CHECK_SIZE(OSCalendarTime, 0x28)
typedef struct OSCalendarTime {
    int32_t tm_sec;
    int32_t tm_min;
    int32_t tm_hour;
    int32_t tm_mday;   // 1-31
    int32_t tm_mon;    // 0-11
    int32_t tm_year;   // anno AD PIENO, non anno-1900 come in C
    int32_t tm_wday;   // 0-6, domenica = 0
    int32_t tm_yday;   // 0-365
    int32_t tm_msec;
    int32_t tm_usec;
} OSCalendarTime;

OSTime  OSGetTime(void);
OSTime  OSGetSystemTime(void);
OSTick  OSGetTick(void);
OSTick  OSGetSystemTick(void);
OSTime  OSCalendarTimeToTicks(OSCalendarTime *calendarTime);
void    OSTicksToCalendarTime(OSTime time, OSCalendarTime *calendarTime);
int32_t __OSSetAbsoluteSystemTime(OSTime time);

#ifdef __cplusplus
}
#endif