#include "internal/symbol_registry.hpp"

#include "coreinit/alarm.h"
#include "coreinit/atomic64.h"
#include "coreinit/cache.h"
#include "coreinit/condition.h"
#include "coreinit/debug.h"
#include "coreinit/event.h"
#include "coreinit/filesystem.h"
#include "coreinit/lockedcache.h"
#include "coreinit/memexpheap.h"
#include "coreinit/memfrmheap.h"
#include "coreinit/memheap.h"
#include "coreinit/memory.h"
#include "coreinit/messagequeue.h"
#include "coreinit/mutex.h"
#include "coreinit/semaphore.h"
#include "coreinit/spinlock.h"
#include "coreinit/systeminfo.h"
#include "coreinit/thread.h"
#include "coreinit/time.h"

#include <cstring>

namespace coreinit_nx {
namespace {

#define EXPORT(lib, fn) { lib, #fn, (void *)(uintptr_t)&fn }

const ExportedSymbol kExports[] = {
    // --- mutex ---
    EXPORT("coreinit", OSInitMutex),
    EXPORT("coreinit", OSInitMutexEx),
    EXPORT("coreinit", OSLockMutex),
    EXPORT("coreinit", OSUnlockMutex),
    EXPORT("coreinit", OSTryLockMutex),

    // --- condition ---
    EXPORT("coreinit", OSInitCond),
    EXPORT("coreinit", OSInitCondEx),
    EXPORT("coreinit", OSWaitCond),
    EXPORT("coreinit", OSSignalCond),

    // --- time ---
    EXPORT("coreinit", OSGetTime),
    EXPORT("coreinit", OSGetSystemTime),
    EXPORT("coreinit", OSGetTick),
    EXPORT("coreinit", OSGetSystemTick),
    EXPORT("coreinit", OSSleepTicks),
    EXPORT("coreinit", OSCalendarTimeToTicks),
    EXPORT("coreinit", OSTicksToCalendarTime),
    EXPORT("coreinit", OSGetSystemInfo),

    // --- heaps ---
    EXPORT("coreinit", MEMCreateExpHeapEx),
    EXPORT("coreinit", MEMDestroyExpHeap),
    EXPORT("coreinit", MEMAllocFromExpHeapEx),
    EXPORT("coreinit", MEMFreeToExpHeap),
    EXPORT("coreinit", MEMGetBaseHeapHandle),
    EXPORT("coreinit", MEMCreateFrmHeapEx),
    EXPORT("coreinit", MEMAllocFromFrmHeapEx),
    EXPORT("coreinit", MEMFreeToFrmHeap),

    // --- filesystem ---
    EXPORT("coreinit", FSInit),
    EXPORT("coreinit", FSAddClient),
    EXPORT("coreinit", FSInitCmdBlock),
    EXPORT("coreinit", FSOpenFile),
    EXPORT("coreinit", FSReadFile),
    EXPORT("coreinit", FSCloseFile),

    // --- events, queues, sync ---
    EXPORT("coreinit", OSInitEvent),
    EXPORT("coreinit", OSSignalEvent),
    EXPORT("coreinit", OSWaitEvent),
    EXPORT("coreinit", OSInitSemaphore),
    EXPORT("coreinit", OSWaitSemaphore),
    EXPORT("coreinit", OSSignalSemaphore),

    // --- debug ---
    EXPORT("coreinit", OSReport),
    EXPORT("coreinit", OSFatal),
    EXPORT("coreinit", OSMemoryBarrier),
};

#undef EXPORT

constexpr size_t kExportCount = sizeof(kExports) / sizeof(kExports[0]);

} // namespace

void *findExport(const char *library, const char *name)
{
    if (!library || !name) return nullptr;
    for (size_t i = 0; i < kExportCount; i++) {
        if (strcmp(kExports[i].library, library) == 0 &&
            strcmp(kExports[i].name, name) == 0) {
            return kExports[i].address;
        }
    }
    return nullptr;
}

bool hasLibrary(const char *library)
{
    if (!library) return false;
    for (size_t i = 0; i < kExportCount; i++) {
        if (strcmp(kExports[i].library, library) == 0) return true;
    }
    return false;
}

size_t exportCount() { return kExportCount; }

} // namespace coreinit_nx