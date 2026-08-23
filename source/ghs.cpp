#include "coreinit/ghs.h"
#include "coreinit/spinlock.h"
#include "coreinit/mutex.h"
#include "coreinit/debug.h"

#include <switch.h>
#include <errno.h>

namespace {

// Layout taken from decaf-emu's coreinit_ghs.cpp: a global spinlock for the
// C library, plus a pool of 100 mutexes indexed by handle for per-FILE
// locking.
constexpr uint32_t kFlockMax = 100;

OSSpinLock g_ghsLock;
OSMutex    g_flockPoolMutex;
OSMutex    g_flocks[kFlockMax];
uint8_t    g_flockInUse[kFlockMax];
uint32_t   g_freeFlockIdx = 0;
bool       g_inited = false;

void ensureInit() {
    if (g_inited) return;
    OSInitSpinLock(&g_ghsLock);
    OSInitMutex(&g_flockPoolMutex);
    for (uint32_t i = 0; i < kFlockMax; i++) g_flockInUse[i] = 0;
    g_freeFlockIdx = 0;
    g_inited = true;
}

} // namespace

extern "C" {

// decaf uses OSUninterruptibleSpinLock here, NOT a mutex -- and spinlocks
// are not recursive, which tells us the GHS C library never nests this
// lock. Worth recording: the obvious worry (printf taking the lock, then
// fwrite taking it again) does not happen.
//
// decaf also excludes these from its HLE trace by default, which is what
// you do for functions called thousands of times a second. They are on the
// hot path of every printf and every errno access.
void __ghsLock(void)
{
    ensureInit();
    OSUninterruptibleSpinLock_Acquire(&g_ghsLock);
}

void __ghsUnlock(void)
{
    ensureInit();
    OSUninterruptibleSpinLock_Release(&g_ghsLock);
}

// Flocks are INDICES into a fixed pool, not pointers.
void __ghs_flock_create(uint32_t *outFlockIdx)
{
    if (!outFlockIdx) return;
    ensureInit();
    OSLockMutex(&g_flockPoolMutex);

    uint32_t idx = g_freeFlockIdx;
    if (idx >= kFlockMax || g_flockInUse[idx]) {
        for (idx = 0; idx < kFlockMax; idx++) {
            if (!g_flockInUse[idx]) break;
        }
        if (idx >= kFlockMax) {
            OSUnlockMutex(&g_flockPoolMutex);
            OSPanic("locks.c", 122, "All flocks are in use");
            return;
        }
    }

    OSInitMutex(&g_flocks[idx]);
    g_flockInUse[idx] = 1;
    *outFlockIdx = idx;
    g_freeFlockIdx = idx + 1;

    OSUnlockMutex(&g_flockPoolMutex);
}

void __ghs_flock_destroy(uint32_t flockIdx)
{
    if (flockIdx >= kFlockMax) return;
    ensureInit();
    g_flockInUse[flockIdx] = 0;
    g_freeFlockIdx = flockIdx;
}

void __ghs_flock_file(uint32_t flockIdx)
{
    if (flockIdx >= kFlockMax) return;
    ensureInit();
    OSLockMutex(&g_flocks[flockIdx]);
}

void __ghs_funlock_file(uint32_t flockIdx)
{
    if (flockIdx >= kFlockMax) return;
    ensureInit();
    OSUnlockMutex(&g_flocks[flockIdx]);
}

// 0 on success, 1234 on failure. The value is arbitrary but it is the real
// one -- taken from decaf-emu, not guessed.
int32_t __ghs_ftrylock_file(uint32_t flockIdx)
{
    if (flockIdx >= kFlockMax) return 1234;
    ensureInit();
    return OSTryLockMutex(&g_flocks[flockIdx]) ? 0 : 1234;
}

// Per-thread, as on Cafe OS: decaf returns &thread->context.error rather
// than a global. newlib's __errno() is already per-thread, so this matches
// without extra work.
int *__gh_errno_ptr(void)
{
    return __errno();
}

int __gh_get_errno(void)
{
    return *__gh_errno_ptr();
}

void __gh_set_errno(int error)
{
    *__gh_errno_ptr() = error;
}

} // extern "C"