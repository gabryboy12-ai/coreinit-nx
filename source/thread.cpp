#include "coreinit/thread.h"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {

constexpr size_t kPageSize     = 0x1000;
constexpr size_t kMinHostStack = 0x10000;

// 16 slot per thread. Un array thread_local invece di appoggiarsi a
    // HostThread: cosi' funziona anche per thread che non abbiamo creato noi.
    constexpr int kThreadSpecificSlots = 16;
    thread_local void *t_specific[kThreadSpecificSlots] = {};

struct HostThread {
    ::Thread             handle;
    OSThread            *guest;                
    OSThreadEntryPointFn entry;
    int32_t              argc;
    const char         **argv;
    void                *guestStack;
    uint32_t             guestStackSize;
    int32_t              exitResult;
    int32_t              suspendCount;
    OSThreadCleanupCallbackFn cleanup;        
    bool                 created;
    bool                 started;
    bool                 detached;
    int32_t               cafePriority;    // come passata dal gioco
    uint32_t              cafeAffinity;    // bitfield originale su 3 core
    const char           *name;
    OSThreadDeallocatorFn deallocator;

    void init() {
        guest = nullptr; entry = nullptr; argc = 0; argv = nullptr;
        guestStack = nullptr; guestStackSize = 0;
        exitResult = 0; suspendCount = 0; cleanup = nullptr;
        created = false; started = false; detached = false;
        cafePriority = 16; cafeAffinity = OS_THREAD_ATTRIB_AFFINITY_ANY;
        name = nullptr; deallocator = nullptr;
    }
};

OSThread g_mainThread;

coreinit_nx::HandleTable<HostThread> g_threads;

// Permette a OSExitThread di sapere in quale thread si trova.
thread_local HostThread *t_current = nullptr;

// libnx vuole void(*)(void*), Cafe OS int(*)(int, const char**).
void trampoline(void *arg)
{
    auto *h = static_cast<HostThread *>(arg);
    t_current = h;
    h->exitResult = h->entry(h->argc, h->argv);
    if (h->cleanup) h->cleanup(h->guest, h->guestStack);
    if (h->deallocator) h->deallocator(h->guest, h->guestStack);
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
    h->guest          = thread;
    h->cafePriority = priority;
    h->cafeAffinity = attributes & OS_THREAD_ATTRIB_AFFINITY_ANY;
    h->name         = nullptr;
    h->deallocator  = nullptr;
    h->cleanup        = nullptr;

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

// Il thread principale non e' stato creato con OSCreateThread, quindi non
// ha un OSThread del guest. Ne forniamo uno sintetico: il codice del gioco
// usa il puntatore come identita' e per passarlo ad altre API, non ne
// ispeziona il contenuto.

OSThread *OSGetCurrentThread(void)
{
    if (t_current) return t_current->guest;

    auto *h = g_threads.get(&g_mainThread);
    if (!h->guest) {
        h->guest   = &g_mainThread;
        h->created = true;
        h->started = true;
    }
    return &g_mainThread;
}

uint32_t OSGetCoreId(void)
{
    // Wii U: 3 core (0-2). Horizon ne espone 4 e ne riserva uno.
    // Il core 3 non esiste su Cafe OS: lo riportiamo come 2.
    const uint32_t id = svcGetCurrentProcessorNumber();
    return id > 2 ? 2 : id;
}

OSThreadCleanupCallbackFn OSSetThreadCleanupCallback(
        OSThread *thread, OSThreadCleanupCallbackFn callback)
{
    auto *h = g_threads.get(thread);
    OSThreadCleanupCallbackFn previous = h->cleanup;
    h->cleanup = callback;
    return previous;
}

// I getter restituiscono quello che il GIOCO ha impostato, non quello che
// ha ottenuto Horizon.
//
// La mappatura delle priorita' e' lossy: 32 livelli Cafe compressi nella
// finestra disponibile. Se interrogassimo il kernel e invertissimo la
// formula, un pattern leggi-modifica-scrivi ("alzati di uno rispetto a
// dove sei") andrebbe alla deriva a ogni giro.
// Stesso ragionamento per l'affinita': il gioco passa una maschera su 3
// core, noi ne scegliamo uno solo. Restituire la maschera originale e'
// l'unica risposta sensata.

int32_t OSGetThreadPriority(OSThread *thread)
{
    return g_threads.get(thread)->cafePriority;
}

int32_t OSSetThreadPriority(OSThread *thread, int32_t priority)
{
    auto *h = g_threads.get(thread);
    h->cafePriority = priority;
    // NON IMPLEMENTATO: cambiare la priorita' di un thread gia' avviato.
    return 1;
}

uint32_t OSGetThreadAffinity(OSThread *thread)
{
    return g_threads.get(thread)->cafeAffinity;
}

int32_t OSSetThreadAffinity(OSThread *thread, uint32_t affinity)
{
    auto *h = g_threads.get(thread);
    h->cafeAffinity = affinity & OS_THREAD_ATTRIB_AFFINITY_ANY;
    // NON IMPLEMENTATO: migrare davvero un thread gia' in esecuzione.
    // Il valore viene ricordato e riportato, ma non applicato.
    return 1;
}

const char *OSGetThreadName(OSThread *thread)
{
    return g_threads.get(thread)->name;
}

void OSSetThreadName(OSThread *thread, const char *name)
{
    // Cafe OS conserva il puntatore, non copia la stringa.
    g_threads.get(thread)->name = name;
}

void *OSGetThreadSpecific(OSThreadSpecificID id)
{
    if ((int)id < 0 || (int)id >= kThreadSpecificSlots) return nullptr;
    return t_specific[(int)id];
}

void OSSetThreadSpecific(OSThreadSpecificID id, void *value)
{
    if ((int)id < 0 || (int)id >= kThreadSpecificSlots) return;
    t_specific[(int)id] = value;
}

OSThreadDeallocatorFn OSSetThreadDeallocator(OSThread *thread,
                                             OSThreadDeallocatorFn fn)
{
    auto *h = g_threads.get(thread);
    OSThreadDeallocatorFn previous = h->deallocator;
    h->deallocator = fn;
    return previous;
}

} // extern "C"