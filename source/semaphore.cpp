#include "coreinit/semaphore.h"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {

// Built on mutex + condvar rather than libnx's Semaphore, because
// OSGetSemaphoreCount must read the count WITHOUT modifying it and libnx
// does not expose the value. Ten extra lines buys us the counter in plain
// sight.
struct HostSemaphore {
    ::Mutex lock;
    CondVar cond;
    int32_t count;

    void init() { mutexInit(&lock); condvarInit(&cond); count = 0; }
};

coreinit_nx::HandleTable<HostSemaphore> g_semaphores;

} // namespace

extern "C" {

void OSInitSemaphore(OSSemaphore *semaphore, int32_t count)
{
    if (!semaphore) return;
    auto *s = g_semaphores.get(semaphore);
    mutexLock(&s->lock);
    s->count = count;
    mutexUnlock(&s->lock);
}

void OSInitSemaphoreEx(OSSemaphore *semaphore, int32_t count, const char *name)
{
    (void)name;
    OSInitSemaphore(semaphore, count);
}

int32_t OSGetSemaphoreCount(OSSemaphore *semaphore)
{
    if (!semaphore) return 0;
    auto *s = g_semaphores.get(semaphore);
    mutexLock(&s->lock);
    const int32_t c = s->count;
    mutexUnlock(&s->lock);
    return c;
}

// Cafe OS returns the count BEFORE the operation.
int32_t OSSignalSemaphore(OSSemaphore *semaphore)
{
    if (!semaphore) return 0;
    auto *s = g_semaphores.get(semaphore);
    mutexLock(&s->lock);
    const int32_t previous = s->count++;
    condvarWakeAll(&s->cond);
    mutexUnlock(&s->lock);
    return previous;
}

int32_t OSWaitSemaphore(OSSemaphore *semaphore)
{
    if (!semaphore) return 0;
    auto *s = g_semaphores.get(semaphore);
    mutexLock(&s->lock);
    while (s->count <= 0) condvarWait(&s->cond, &s->lock);
    const int32_t previous = s->count--;
    mutexUnlock(&s->lock);
    return previous;
}

// wut: "If the value is >0 then it means the call was successful."
// So the return is the PREVIOUS count, not a boolean.
int32_t OSTryWaitSemaphore(OSSemaphore *semaphore)
{
    if (!semaphore) return 0;
    auto *s = g_semaphores.get(semaphore);
    mutexLock(&s->lock);
    const int32_t previous = s->count;
    if (s->count > 0) s->count--;
    mutexUnlock(&s->lock);
    return previous;
}

} // extern "C"