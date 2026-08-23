#include "internal/symbol_registry.hpp"

#include "coreinit/alarm.h"
#include "coreinit/atomic64.h"
#include "coreinit/cache.h"
#include "coreinit/condition.h"
#include "coreinit/debug.h"
#include "coreinit/event.h"
#include "coreinit/filesystem.h"
#include "coreinit/ghs.h"
#include "coreinit/lockedcache.h"
#include "coreinit/memdefaultheap.h"
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
#include "coreinit/mcp.h"
#include "coreinit/userconfig.h"
#include "coreinit/exception.h"
#include "coreinit/foreground.h"
#include "coreinit/driver.h"



#include <cstring>

namespace coreinit_nx {
namespace {

#define EXPORT_FN(lib, fn)   { lib, #fn, (void *)(uintptr_t)&fn, ExportKind::Function }
// Per i dati esportiamo l'INDIRIZZO DELLA VARIABILE, non il suo contenuto:
// il gioco vuole poter leggere e riscrivere il puntatore.
#define EXPORT_DATA(lib, v)  { lib, #v,  (void *)&v,             ExportKind::Data }

const ExportedSymbol kExports[] = {
    // --- mutex ---
    EXPORT_FN("coreinit", OSInitMutex),
    EXPORT_FN("coreinit", OSInitMutexEx),
    EXPORT_FN("coreinit", OSLockMutex),
    EXPORT_FN("coreinit", OSUnlockMutex),
    EXPORT_FN("coreinit", OSTryLockMutex),

    // --- condition ---
    EXPORT_FN("coreinit", OSInitCond),
    EXPORT_FN("coreinit", OSInitCondEx),
    EXPORT_FN("coreinit", OSWaitCond),
    EXPORT_FN("coreinit", OSSignalCond),

    // --- time ---
    EXPORT_FN("coreinit", OSGetTime),
    EXPORT_FN("coreinit", OSGetSystemTime),
    EXPORT_FN("coreinit", OSGetTick),
    EXPORT_FN("coreinit", OSGetSystemTick),
    EXPORT_FN("coreinit", OSSleepTicks),
    EXPORT_FN("coreinit", OSCalendarTimeToTicks),
    EXPORT_FN("coreinit", OSTicksToCalendarTime),
    EXPORT_FN("coreinit", OSGetSystemInfo),

    // --- heaps ---
    EXPORT_FN("coreinit", MEMCreateExpHeapEx),
    EXPORT_FN("coreinit", MEMDestroyExpHeap),
    EXPORT_FN("coreinit", MEMAllocFromExpHeapEx),
    EXPORT_FN("coreinit", MEMFreeToExpHeap),
    EXPORT_FN("coreinit", MEMGetBaseHeapHandle),
    EXPORT_FN("coreinit", MEMCreateFrmHeapEx),
    EXPORT_FN("coreinit", MEMAllocFromFrmHeapEx),
    EXPORT_FN("coreinit", MEMFreeToFrmHeap),

    // --- filesystem ---
    EXPORT_FN("coreinit", FSInit),
    EXPORT_FN("coreinit", FSAddClient),
    EXPORT_FN("coreinit", FSInitCmdBlock),
    EXPORT_FN("coreinit", FSOpenFile),
    EXPORT_FN("coreinit", FSReadFile),
    EXPORT_FN("coreinit", FSCloseFile),

    // --- events, queues, sync ---
    EXPORT_FN("coreinit", OSInitEvent),
    EXPORT_FN("coreinit", OSSignalEvent),
    EXPORT_FN("coreinit", OSWaitEvent),
    EXPORT_FN("coreinit", OSInitSemaphore),
    EXPORT_FN("coreinit", OSWaitSemaphore),
    EXPORT_FN("coreinit", OSSignalSemaphore),

    // --- debug ---
    EXPORT_FN("coreinit", OSReport),
    EXPORT_FN("coreinit", OSFatal),
    EXPORT_FN("coreinit", OSMemoryBarrier),

        // --- GHS C library runtime ---
    EXPORT_FN("coreinit", __ghsLock),
    EXPORT_FN("coreinit", __ghsUnlock),
    EXPORT_FN("coreinit", __gh_errno_ptr),
    EXPORT_FN("coreinit", __gh_get_errno),
    EXPORT_FN("coreinit", __gh_set_errno),
    EXPORT_FN("coreinit", __ghs_flock_create),
    EXPORT_FN("coreinit", __ghs_flock_file),
    EXPORT_FN("coreinit", __ghs_funlock_file),

        // --- system configuration ---
    EXPORT_FN("coreinit", UCOpen),
    EXPORT_FN("coreinit", UCClose),
    EXPORT_FN("coreinit", UCReadSysConfig),
    EXPORT_FN("coreinit", UCWriteSysConfig),
    EXPORT_FN("coreinit", MCP_Open),
    EXPORT_FN("coreinit", MCP_Close),
    EXPORT_FN("coreinit", MCP_GetSysProdSettings),

        // --- misc ---
    EXPORT_FN("coreinit", OSYieldThread),
    EXPORT_FN("coreinit", PPCSync),
    EXPORT_FN("coreinit", OSCompareAndSwapAtomicEx64),
    EXPORT_FN("coreinit", OSEnableHomeButtonMenu),
    EXPORT_FN("coreinit", OSSetExceptionCallback),
    EXPORT_FN("coreinit", ENVGetEnvironmentVariable),

    EXPORT_FN("coreinit", OSDriver_Register),
    EXPORT_FN("coreinit", OSDriver_Deregister),

    EXPORT_FN("coreinit", FSOpenFileAsync),
    EXPORT_FN("coreinit", FSReadFileWithPosAsync),
    EXPORT_FN("coreinit", FSCloseFileAsync),
    EXPORT_FN("coreinit", __ghs_mtx_init),
    EXPORT_FN("coreinit", __ghs_mtx_lock),
    EXPORT_FN("coreinit", __ghs_mtx_unlock),
    EXPORT_FN("coreinit", MEMFindContainHeap),

        // --- data exports ---
    EXPORT_DATA("coreinit", MEMAllocFromDefaultHeap),
    EXPORT_DATA("coreinit", MEMAllocFromDefaultHeapEx),
    EXPORT_DATA("coreinit", MEMFreeToDefaultHeap),
};

#undef EXPORT_FN

constexpr size_t kExportCount = sizeof(kExports) / sizeof(kExports[0]);

} // namespace

void *findExportOfKind(const char *library, const char *name, ExportKind kind)
{
    if (!library || !name) return nullptr;
    for (size_t i = 0; i < kExportCount; i++) {
        if (kExports[i].kind == kind &&
            strcmp(kExports[i].library, library) == 0 &&
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