#include "coreinit/alarm.h"
#include "coreinit/time.h"
#include "internal/time_internal.hpp"

#include <switch.h>
#include <algorithm>
#include <map>
#include <vector>

namespace {

// One scheduler thread for all alarms, not one thread per alarm: a game may
// create dozens, and dozens of sleeping threads waste memory and scheduling.
//
// It sleeps on a condition variable with a timeout until the nearest due
// alarm, so there is no polling loop. Setting a sooner alarm signals the
// condvar and the scheduler recomputes.
//
// The thread starts lazily on the first OSSetAlarm, so a game that never
// uses alarms pays nothing.

struct HostAlarm {
    OSTime          due;        // scadenza assoluta in tick Cafe
    OSTime          interval;   // 0 = una tantum
    OSAlarmCallback callback;
    void           *userData;
    uint32_t        group;
    bool            armed;
};

std::map<OSAlarm *, HostAlarm> g_alarms;
::Mutex   g_lock;
CondVar   g_wake;
::Thread  g_thread;
bool      g_started = false;
bool      g_stop    = false;
bool      g_inited  = false;

void ensureInit() {
    if (!g_inited) { mutexInit(&g_lock); condvarInit(&g_wake); g_inited = true; }
}

HostAlarm *find(OSAlarm *a) {
    auto it = g_alarms.find(a);
    return it == g_alarms.end() ? nullptr : &it->second;
}

void schedulerLoop(void *)
{
    mutexLock(&g_lock);
    while (!g_stop) {
        const OSTime now = OSGetTime();

        // Raccogli gli scaduti PRIMA di invocarli: la callback puo'
        // impostare o cancellare allarmi, quindi la mappa puo' cambiare.
        std::vector<std::pair<OSAlarm *, OSAlarmCallback>> due;
        OSTime nearest = 0;

        for (auto &entry : g_alarms) {
            HostAlarm &h = entry.second;
            if (!h.armed) continue;
            if (h.due <= now) {
                due.push_back({entry.first, h.callback});
                if (h.interval > 0) h.due = now + h.interval;
                else                h.armed = false;
            }
            if (h.armed && (nearest == 0 || h.due < nearest)) nearest = h.due;
        }

        if (!due.empty()) {
            // Le callback vanno invocate SENZA il lock: una callback che
            // imposta un altro allarme andrebbe altrimenti in deadlock.
            mutexUnlock(&g_lock);
            for (auto &d : due) if (d.second) d.second(d.first, nullptr);
            mutexLock(&g_lock);
            continue;   // ricalcola: la mappa puo' essere cambiata
        }

        if (nearest == 0) {
            condvarWait(&g_wake, &g_lock);          // nessun allarme armato
        } else {
            const OSTime delta = nearest - OSGetTime();
            const u64 ns = delta > 0
                ? (u64)delta * 32000ull / 1989ull   // tick Cafe -> ns
                : 0;
            condvarWaitTimeout(&g_wake, &g_lock, ns);
        }
    }
    mutexUnlock(&g_lock);
}

void ensureThread() {
    if (g_started) return;
    s32 prio = 0x2C;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (R_SUCCEEDED(threadCreate(&g_thread, schedulerLoop, nullptr, nullptr,
                                 0x8000, prio, -2)) &&
        R_SUCCEEDED(threadStart(&g_thread))) {
        g_started = true;
    }
}

} // namespace

extern "C" {

void OSCreateAlarm(OSAlarm *alarm)
{
    if (!alarm) return;
    ensureInit();
    mutexLock(&g_lock);
    g_alarms[alarm] = HostAlarm{0, 0, nullptr, nullptr, 0, false};
    mutexUnlock(&g_lock);
}

void OSCreateAlarmEx(OSAlarm *alarm, const char *name)
{
    (void)name;
    OSCreateAlarm(alarm);
}

int32_t OSSetAlarm(OSAlarm *alarm, OSTime time, OSAlarmCallback callback)
{
    if (!alarm) return 0;
    ensureInit();
    mutexLock(&g_lock);
    HostAlarm &h = g_alarms[alarm];
    // ASSUNZIONE: 'time' e' una scadenza ASSOLUTA in tick, non un delta.
        // wut documenta 'start' di OSSetPeriodicAlarm come "la durata fino al
    // primo scatto": il tempo e' RELATIVO, non una scadenza assoluta.
    // Verificato con un test che passa un valore sensato solo come delta.
    h.due = OSGetTime() + time; h.interval = 0; h.callback = callback; h.armed = true;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
    ensureThread();
    return 1;
}

int32_t OSSetPeriodicAlarm(OSAlarm *alarm, OSTime start, OSTime interval,
                           OSAlarmCallback callback)
{
    if (!alarm) return 0;
    ensureInit();
    mutexLock(&g_lock);
    HostAlarm &h = g_alarms[alarm];
    h.due = OSGetTime() + start; h.interval = interval; h.callback = callback; h.armed = true;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
    ensureThread();
    return 1;
}

int32_t OSCancelAlarm(OSAlarm *alarm)
{
    if (!alarm) return 0;
    ensureInit();
    mutexLock(&g_lock);
    HostAlarm *h = find(alarm);
    const bool was = h && h->armed;
    if (h) h->armed = false;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
    return was ? 1 : 0;
}

void OSCancelAlarms(uint32_t group)
{
    ensureInit();
    mutexLock(&g_lock);
    for (auto &e : g_alarms) if (e.second.group == group) e.second.armed = false;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
}

void OSSetAlarmTag(OSAlarm *alarm, uint32_t group)
{
    if (!alarm) return;
    ensureInit();
    mutexLock(&g_lock);
    g_alarms[alarm].group = group;
    mutexUnlock(&g_lock);
}

void OSSetAlarmUserData(OSAlarm *alarm, void *data)
{
    if (!alarm) return;
    ensureInit();
    mutexLock(&g_lock);
    g_alarms[alarm].userData = data;
    mutexUnlock(&g_lock);
}

void *OSGetAlarmUserData(OSAlarm *alarm)
{
    if (!alarm) return nullptr;
    ensureInit();
    mutexLock(&g_lock);
    HostAlarm *h = find(alarm);
    void *d = h ? h->userData : nullptr;
    mutexUnlock(&g_lock);
    return d;
}

// Attesa attiva a granularita' grossa invece di una condvar per allarme:
// OSWaitAlarm non compare nel censimento, quindi non vale la complessita'.
int32_t OSWaitAlarm(OSAlarm *alarm)
{
    if (!alarm) return 0;
    while (true) {
        ensureInit();
        mutexLock(&g_lock);
        HostAlarm *h = find(alarm);
        const bool armed = h && h->armed;
        mutexUnlock(&g_lock);
        if (!armed) return 1;
        svcSleepThread(1000000ULL);   // 1 ms
    }
}

void coreinitNxAlarmShutdown(void)
{
    if (!g_inited) return;

    mutexLock(&g_lock);
    g_stop = true;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);

    if (g_started) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_started = false;
    }

    mutexLock(&g_lock);
    g_alarms.clear();
    g_stop = false;
    mutexUnlock(&g_lock);
}

} // extern "C"