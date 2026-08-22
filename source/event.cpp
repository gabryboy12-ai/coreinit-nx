#include "coreinit/event.h"
#include "internal/handle_table.hpp"
#include "internal/time_internal.hpp"

#include <switch.h>

namespace {

// Cafe OS events are Win32-style: manual-reset stays signalled until
// OSResetEvent, auto-reset releases exactly one waiter and clears itself.
// wut's documentation links each function to its Windows counterpart.
struct HostEvent {
    ::Mutex lock;
    CondVar cond;
    bool    signalled;
    bool    autoReset;

    void init() {
        mutexInit(&lock); condvarInit(&cond);
        signalled = false; autoReset = false;
    }
};

coreinit_nx::HandleTable<HostEvent> g_events;

} // namespace

extern "C" {

void OSInitEvent(OSEvent *event, int32_t value, OSEventMode mode)
{
    if (!event) return;
    auto *e = g_events.get(event);
    mutexLock(&e->lock);
    e->signalled = (value != 0);
    e->autoReset = (mode == OS_EVENT_MODE_AUTO);
    mutexUnlock(&e->lock);
}

void OSInitEventEx(OSEvent *event, int32_t value, OSEventMode mode,
                   const char *name)
{
    (void)name;
    OSInitEvent(event, value, mode);
}

void OSSignalEvent(OSEvent *event)
{
    if (!event) return;
    auto *e = g_events.get(event);
    mutexLock(&e->lock);
    e->signalled = true;
    // Auto-reset: sveglia UN solo waiter, che poi azzera lo stato.
    // Manuale: restano tutti liberi finche' non arriva OSResetEvent.
    if (e->autoReset) condvarWakeOne(&e->cond);
    else              condvarWakeAll(&e->cond);
    mutexUnlock(&e->lock);
}

void OSSignalEventAll(OSEvent *event)
{
    if (!event) return;
    auto *e = g_events.get(event);
    mutexLock(&e->lock);
    e->signalled = true;
    condvarWakeAll(&e->cond);
    // wut: con auto-reset, tutti i thread in attesa vengono svegliati e
    // l'evento viene poi resettato.
    if (e->autoReset) e->signalled = false;
    mutexUnlock(&e->lock);
}

void OSWaitEvent(OSEvent *event)
{
    if (!event) return;
    auto *e = g_events.get(event);
    mutexLock(&e->lock);
    while (!e->signalled) condvarWait(&e->cond, &e->lock);
    if (e->autoReset) e->signalled = false;
    mutexUnlock(&e->lock);
}

void OSResetEvent(OSEvent *event)
{
    if (!event) return;
    auto *e = g_events.get(event);
    mutexLock(&e->lock);
    e->signalled = false;
    mutexUnlock(&e->lock);
}

int32_t OSWaitEventWithTimeout(OSEvent *event, OSTime timeout)
{
    if (!event) return 0;
    auto *e = g_events.get(event);
    const u64 ns = timeout > 0 ? (u64)timeout * 32000ull / 1989ull : 0;
    const u64 deadline = armGetSystemTick() + ns * 192ull / 10000ull;

    mutexLock(&e->lock);
    while (!e->signalled) {
        if (armGetSystemTick() >= deadline) {
            mutexUnlock(&e->lock);
            return 0;                     // scaduto
        }
        condvarWaitTimeout(&e->cond, &e->lock, 1000000ULL);   // 1 ms
    }
    if (e->autoReset) e->signalled = false;
    mutexUnlock(&e->lock);
    return 1;
}

} // extern "C"