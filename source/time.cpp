#include "coreinit/time.h"
#include "coreinit/systeminfo.h"
#include "internal/time_internal.hpp"

#include <switch.h>

namespace coreinit_nx {

// Horizon conta a 19.2 MHz, il Wii U a 62'156'250 Hz.
// 62156250 / 19200000 si riduce esattamente a 3315/1024:
// conversione esatta, senza virgola mobile, e nessun overflow
// prima di circa nove anni di uptime.
int64_t systemTimeTicks()
{
    return (int64_t)((armGetSystemTick() * 3315ull) / 1024ull);
}

int64_t computeBaseTime()
{
    u64 unixSeconds = 0;
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &unixSeconds))) {
        return 0;   // orologio non disponibile: si parte dall'epoca
    }
    const int64_t cafeSeconds = (int64_t)unixSeconds - kUnixToCafeEpoch;
    return cafeSeconds * (int64_t)kWiiUTimerHz - systemTimeTicks();
}

} // namespace coreinit_nx

namespace {

// Algoritmo giorni-da-data civile (Howard Hinnant), valido per
// qualunque data proleptica gregoriana. Preferito a gmtime/mktime
// perche' non trascina fusi orari ne' stato globale della libc.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int32_t *y, int32_t *m, int32_t *d)
{
    z += 719468;
    const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe/1460u + doe/36524u - doe/146096u) / 365u;
    const int64_t  yr  = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365u*yoe + yoe/4u - yoe/100u);
    const unsigned mp  = (5u*doy + 2u) / 153u;
    const unsigned dd  = doy - (153u*mp + 2u)/5u + 1u;
    const unsigned mm  = mp + (mp < 10u ? 3u : (unsigned)-9);
    *y = (int32_t)(yr + (mm <= 2u));
    *m = (int32_t)mm;
    *d = (int32_t)dd;
}

} // namespace

extern "C" {

OSTime OSGetSystemTime(void)
{
    return coreinit_nx::systemTimeTicks();
}

OSTime OSGetTime(void)
{
    return OSGetSystemInfo()->baseTime + OSGetSystemTime();
}

OSTick OSGetSystemTick(void)
{
    return (OSTick)(uint32_t)OSGetSystemTime();
}

OSTick OSGetTick(void)
{
    return (OSTick)(uint32_t)OSGetTime();
}

void OSTicksToCalendarTime(OSTime time, OSCalendarTime *ct)
{
    if (!ct) return;

    const int64_t hz = (int64_t)coreinit_nx::kWiiUTimerHz;
    int64_t secs = time / hz;
    int64_t rem  = time % hz;
    if (rem < 0) { rem += hz; secs--; }

    const int64_t unixSecs = secs + coreinit_nx::kUnixToCafeEpoch;
    int64_t days = unixSecs / 86400;
    int64_t sod  = unixSecs % 86400;
    if (sod < 0) { sod += 86400; days--; }

    int32_t y, m, d;
    civilFromDays(days, &y, &m, &d);

    ct->tm_year = y;
    ct->tm_mon  = m - 1;
    ct->tm_mday = d;
    ct->tm_hour = (int32_t)(sod / 3600);
    ct->tm_min  = (int32_t)((sod % 3600) / 60);
    ct->tm_sec  = (int32_t)(sod % 60);
    ct->tm_wday = (int32_t)(((days % 7) + 11) % 7);   // 1970-01-01 = giovedi
    ct->tm_yday = (int32_t)(days - daysFromCivil(y, 1, 1));

    const uint64_t usec = (uint64_t)rem * 1000000ull / (uint64_t)hz;
    ct->tm_msec = (int32_t)(usec / 1000);
    ct->tm_usec = (int32_t)(usec % 1000);
}

OSTime OSCalendarTimeToTicks(OSCalendarTime *ct)
{
    if (!ct) return 0;

    const int64_t hz   = (int64_t)coreinit_nx::kWiiUTimerHz;
    const int64_t days = daysFromCivil(ct->tm_year,
                                       (unsigned)(ct->tm_mon + 1),
                                       (unsigned)ct->tm_mday);
    const int64_t unixSecs = days * 86400
                           + ct->tm_hour * 3600
                           + ct->tm_min * 60
                           + ct->tm_sec;
    const int64_t cafeSecs = unixSecs - coreinit_nx::kUnixToCafeEpoch;

    int64_t ticks = cafeSecs * hz;
    ticks += ((int64_t)ct->tm_msec * 1000 + ct->tm_usec) * hz / 1000000;
    return ticks;
}

int32_t __OSSetAbsoluteSystemTime(OSTime time)
{
    OSGetSystemInfo()->baseTime = time - OSGetSystemTime();
    return 1;
}

void OSSleepTicks(OSTime ticks)
{
    if (ticks <= 0) return;
    // 1e9 / 62156250 riduce esattamente a 32000/1989.
    // E' la stessa identita' usata dalla macro OSTicksToNanoseconds di wut.
    svcSleepThread((u64)ticks * 32000ull / 1989ull);
}

} // extern "C"