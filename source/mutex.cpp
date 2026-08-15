#include "coreinit/mutex.h"
#include "internal/mutex_internal.hpp"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {
coreinit_nx::HandleTable<coreinit_nx::HostMutex> g_mutexes;
}

namespace coreinit_nx {
HostMutex *getHostMutex(const OSMutex *mutex) { return g_mutexes.get(mutex); }
}

extern "C" {

void OSInitMutex(OSMutex *mutex)
{
    coreinit_nx::getHostMutex(mutex);
}

void OSInitMutexEx(OSMutex *mutex, const char *name)
{
    (void)name;
    coreinit_nx::getHostMutex(mutex);
}

void OSLockMutex(OSMutex *mutex)
{
    auto *h = coreinit_nx::getHostMutex(mutex);
    if (mutexIsLockedByCurrentThread(&h->lock)) {
        h->count++;          // rientro: non tocchiamo il kernel
        return;
    }
    mutexLock(&h->lock);
    h->count = 1;
}

void OSUnlockMutex(OSMutex *mutex)
{
    auto *h = coreinit_nx::getHostMutex(mutex);
    if (!mutexIsLockedByCurrentThread(&h->lock)) {
        return;              // difensivo: unlock non bilanciato
    }
    if (--h->count == 0) {
        mutexUnlock(&h->lock);
    }
}

int32_t OSTryLockMutex(OSMutex *mutex)
{
    auto *h = coreinit_nx::getHostMutex(mutex);
    if (mutexIsLockedByCurrentThread(&h->lock)) {
        h->count++;
        return 1;
    }
    if (mutexTryLock(&h->lock)) {
        h->count = 1;
        return 1;
    }
    return 0;
}

} // extern "C"