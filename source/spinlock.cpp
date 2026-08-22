#include "coreinit/spinlock.h"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {

// Cafe OS spinlocks disable interrupts and busy-wait, which suits a system
// with few cores and cooperative scheduling.
//
// Under Horizon interrupts cannot be disabled from userspace, and busy
// waiting would be WORSE than a mutex: the Tegra has fewer usable cores
// than the Wii U, so a spinning thread steals time from the very thread
// holding the lock. On preemptive scheduling, spinning is a pessimisation.
//
// Mapped to ordinary mutexes: correct behaviour, better performance here.
struct HostSpinLock {
    ::Mutex lock;
    void init() { mutexInit(&lock); }
};

coreinit_nx::HandleTable<HostSpinLock> g_spinlocks;

int32_t acquire(OSSpinLock *l) {
    if (!l) return 0;
    mutexLock(&g_spinlocks.get(l)->lock);
    return 1;
}

int32_t tryAcquire(OSSpinLock *l) {
    if (!l) return 0;
    return mutexTryLock(&g_spinlocks.get(l)->lock) ? 1 : 0;
}

int32_t tryAcquireTimeout(OSSpinLock *l, OSTime timeout) {
    if (!l) return 0;
    // Nessun mutexLockTimeout in libnx: ritentiamo a intervalli brevi.
    const u64 deadline = armGetSystemTick() +
                         (u64)(timeout > 0 ? timeout : 0) * 1024ull / 3315ull;
    while (true) {
        if (mutexTryLock(&g_spinlocks.get(l)->lock)) return 1;
        if (armGetSystemTick() >= deadline) return 0;
        svcSleepThread(100000ULL);   // 0.1 ms
    }
}

int32_t release(OSSpinLock *l) {
    if (!l) return 0;
    mutexUnlock(&g_spinlocks.get(l)->lock);
    return 1;
}

} // namespace

extern "C" {

void    OSInitSpinLock(OSSpinLock *l)      { if (l) g_spinlocks.get(l); }
int32_t OSAcquireSpinLock(OSSpinLock *l)   { return acquire(l); }
int32_t OSTryAcquireSpinLock(OSSpinLock *l){ return tryAcquire(l); }
int32_t OSReleaseSpinLock(OSSpinLock *l)   { return release(l); }

int32_t OSTryAcquireSpinLockWithTimeout(OSSpinLock *l, OSTime timeout)
{ return tryAcquireTimeout(l, timeout); }

// Le varianti "uninterruptible" sono identiche: la differenza su Cafe OS
// era la disabilitazione degli interrupt, che qui non esiste.
int32_t OSUninterruptibleSpinLock_Acquire(OSSpinLock *l)    { return acquire(l); }
int32_t OSUninterruptibleSpinLock_TryAcquire(OSSpinLock *l) { return tryAcquire(l); }
int32_t OSUninterruptibleSpinLock_Release(OSSpinLock *l)    { return release(l); }

int32_t OSUninterruptibleSpinLock_TryAcquireWithTimeout(OSSpinLock *l,
                                                        OSTime timeout)
{ return tryAcquireTimeout(l, timeout); }

} // extern "C"