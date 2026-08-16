#include "coreinit/systeminfo.h"
#include "internal/time_internal.hpp"

#include <switch.h>

namespace {
OSSystemInfo g_info;
bool         g_ready = false;
::Mutex      g_lock;
bool         g_lockInit = false;
}

extern "C" OSSystemInfo *OSGetSystemInfo(void)
{
    if (!g_lockInit) { mutexInit(&g_lock); g_lockInit = true; }

    mutexLock(&g_lock);
    if (!g_ready) {
        g_info.busClockSpeed  = coreinit_nx::kWiiUBusClockHz;
        g_info.coreClockSpeed = coreinit_nx::kWiiUCoreClockHz;
        g_info.baseTime       = coreinit_nx::computeBaseTime();
        for (unsigned i = 0; i < sizeof(g_info._unknown); i++) {
            g_info._unknown[i] = 0;
        }
        g_ready = true;
    }
    mutexUnlock(&g_lock);
    return &g_info;
}

extern "C" uint64_t OSGetTitleID(void)
{
    // Non c'e' un vero title ID Wii U qui. I giochi lo usano per nominare
    // i salvataggi e per il logging. Restituiamo 0: dovra' diventare
    // configurabile, impostato dal port a runtime.
    return 0;
}