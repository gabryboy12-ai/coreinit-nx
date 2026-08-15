#include "coreinit/thread.h"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {

constexpr size_t kPageSize     = 0x1000;
constexpr size_t kMinHostStack = 0x10000;

struct HostThread {
    ::Thread             handle;
    OSThreadEntryPointFn entry;
    int32_t              argc;
    const char         **argv;
    void                *guestStack;      // conservato, vedi note di design
    uint32_t             guestStackSize;
    int32_t              exitResult;
    int32_t              suspendCount;
    bool                 created;
    bool                 started;
    bool                 detached;

    void init() {
        entry = nullptr; argc = 0; argv = nullptr;
        guestStack = nullptr; guestStackSize = 0;
        exitResult = 0; suspendCount = 0;
        created = false; started = false; detached = false;
    }
};

coreinit_nx::HandleTable<HostThread> g_threads;

// Permette a OSExitThread di sapere in quale thread si trova.
thread_local HostThread *t_current = nullptr;

// libnx vuole void(*)(void*), Cafe OS int(*)(int, const char**).
void trampoline(void *arg)
{
    auto *h = static_cast<HostThread *>(arg);
    t_current = h;
    h->exitResult = h->entry(h->argc, h->argv);
}

// Cafe OS: 0-31, piu' basso = piu' prioritario.
// Horizon: 0-0x3F, stessa convenzione, ma non si puo' fare meglio
// della priorita' del processo. Mappatura lineare nella finestra utile.
int32_t mapPriority(int32_t cafePriority)
{
    s32 processPrio = 0x2C;
    if (R_FAILED(svcGetThreadPriority(&processPrio, CUR_THREAD_HANDLE))) {
        processPrio = 0x2C;
    }
    if (cafePriority < 0)  cafePriority = 0;
    if (cafePriority > 31) cafePriority = 31;
    const int32_t span = 0x3F - processPrio;
    return processPrio + (cafePriority * span) / 31;
}

// Bitfield -> singolo core. libnx non accetta maschere.
int mapAffinity(OSThreadAttributes attr)
{
    const uint8_t mask = attr & OS_THREAD_ATTRIB_AFFINITY_ANY;
    if (mask == 0 || mask == OS_THREAD_ATTRIB_AFFINITY_ANY) return -2;
    if (mask & OS_THREAD_ATTRIB_AFFINITY_CPU0) return 0;
    if (mask & OS_THREAD_ATTRIB_AFFINITY_CPU1) return 1;
    return 2;
}

size_t hostStackSize(uint32_t guestStackSize)
{
    size_t sz = guestStackSize < kMinHostStack ? kMinHostStack : guestStackSize;
    return (sz + kPageSize - 1) & ~(kPageSize - 1);
}

} // namespace

extern "C" {

int32_t OSCreateThread(OSThread *thread, OSThreadEntryPointFn entry,
                       int32_t argc, char *argv,
                       void *stack, uint32_t stackSize,
                       int32_t priority, OSThreadAttributes attributes)
{
    auto *h = g_threads.get(thread);
    h->entry          = entry;
    h->argc           = argc;
    h->argv           = reinterpret_cast<const char **>(argv);
    h->guestStack     = stack;
    h->guestStackSize = stackSize;
    h->exitResult     = 0;
    h->suspendCount   = 1;        // Cafe OS crea SOSPESO
    h->started        = false;
    h->detached       = (attributes & OS_THREAD_ATTRIB_DETACHED) != 0;

    const int32_t prio    = mapPriority(priority);
    const size_t  stackSz = hostStackSize(stackSize);

    int cpu = mapAffinity(attributes);
    Result rc = threadCreate(&h->handle, trampoline, h, nullptr, stackSz, prio, cpu);
    if (R_FAILED(rc) && cpu != -2) {
        cpu = -2;                 // ripiego sul core predefinito
        rc  = threadCreate(&h->handle, trampoline, h, nullptr, stackSz, prio, cpu);
    }

    h->created = R_SUCCEEDED(rc);
    return h->created ? 1 : 0;
}

int32_t OSResumeThread(OSThread *thread)
{
    auto *h = g_threads.get(thread);
    const int32_t previous = h->suspendCount;

    if (h->suspendCount > 0) h->suspendCount--;

    if (h->suspendCount == 0 && h->created && !h->started) {
        if (R_SUCCEEDED(threadStart(&h->handle))) h->started = true;
    }
    return previous;
}

int32_t OSSuspendThread(OSThread *thread)
{
    auto *h = g_threads.get(thread);
    const int32_t previous = h->suspendCount;
    h->suspendCount++;
    // NON IMPLEMENTATO: sospendere un thread gia' in esecuzione.
    return previous;
}

int32_t OSJoinThread(OSThread *thread, int *threadResult)
{
    auto *h = g_threads.get(thread);
    if (!h->created || !h->started || h->detached) return 0;
    if (R_FAILED(threadWaitForExit(&h->handle))) return 0;

    if (threadResult) *threadResult = h->exitResult;

    threadClose(&h->handle);
    h->started = false;
    h->created = false;
    return 1;
}

void OSExitThread(int32_t result)
{
    if (t_current) t_current->exitResult = result;
    threadExit();
}

} // extern "C"