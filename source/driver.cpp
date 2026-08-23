#include "coreinit/driver.h"

#include <switch.h>
#include <cstring>

namespace {

// Cafe OS drivers get lifecycle callbacks: onInit at registration, then
// onAcquiredForeground / onReleasedForeground as the system menu takes the
// screen, and onDone at shutdown.
//
// Horizon has no equivalent transition -- application lifecycle runs
// through appletMainLoop and nothing corresponds to the Wii U's foreground
// handover. So the foreground callbacks are registered and NEVER invoked.
//
// That is deliberate. Inventing transitions would be worse than silence: a
// driver receiving an unexpected onReleasedForeground might free resources
// that are still in use.
//
// onInit is different -- it fires at registration, a moment we do have.

constexpr int kMaxDrivers = 32;
constexpr uint32_t kSaveAreaSize = 256;

struct RegisteredDriver {
    OSDynLoad_ModuleHandle moduleHandle;
    OSDriver_UserDriverId  userDriverId;
    OSDriverInterface      iface;
    uint8_t                saveArea[kSaveAreaSize];
    uint32_t               saveAreaUsed;
    bool                   used;
};

RegisteredDriver g_drivers[kMaxDrivers];
::Mutex g_lock;
bool    g_inited = false;

void ensureInit() {
    if (!g_inited) { mutexInit(&g_lock); g_inited = true; }
}

RegisteredDriver *find(OSDriver_UserDriverId id) {
    for (int i = 0; i < kMaxDrivers; i++) {
        if (g_drivers[i].used && g_drivers[i].userDriverId == id) {
            return &g_drivers[i];
        }
    }
    return nullptr;
}

} // namespace

extern "C" {

OSDriver_Error OSDriver_Register(OSDynLoad_ModuleHandle moduleHandle,
                                 uint32_t unk1,
                                 OSDriverInterface *driverInterface,
                                 OSDriver_UserDriverId userDriverId,
                                 uint32_t *outRegisteredUpid,
                                 uint32_t *outOwnerUpid,
                                 int32_t *outDidOSDriverInit)
{
    (void)unk1;
    if (!driverInterface) return -1;

    ensureInit();
    mutexLock(&g_lock);

    int slot = -1;
    for (int i = 0; i < kMaxDrivers; i++) {
        if (!g_drivers[i].used) { slot = i; break; }
    }
    if (slot < 0) { mutexUnlock(&g_lock); return -1; }

    g_drivers[slot].moduleHandle = moduleHandle;
    g_drivers[slot].userDriverId = userDriverId;
    g_drivers[slot].iface        = *driverInterface;
    g_drivers[slot].saveAreaUsed = 0;
    memset(g_drivers[slot].saveArea, 0, kSaveAreaSize);
    g_drivers[slot].used = true;

    const OSDriver_OnInitFn onInit = driverInterface->onInit;
    mutexUnlock(&g_lock);

    if (outRegisteredUpid)  *outRegisteredUpid = 0;
    if (outOwnerUpid)       *outOwnerUpid = 0;
    if (outDidOSDriverInit) *outDidOSDriverInit = 1;

    // Fuori dal lock: la callback puo' registrare altri driver.
    if (onInit) onInit(userDriverId);

    return 0;
}

OSDriver_Error OSDriver_Deregister(OSDynLoad_ModuleHandle moduleHandle,
                                   OSDriver_UserDriverId userDriverId)
{
    (void)moduleHandle;
    ensureInit();
    mutexLock(&g_lock);
    RegisteredDriver *d = find(userDriverId);
    if (d) d->used = false;
    mutexUnlock(&g_lock);
    return d ? 0 : -1;
}

// A small per-driver scratch area that survives across calls. On Cafe OS it
// survived a foreground handover; here nothing takes the foreground, so it
// simply persists.
OSDriver_Error OSDriver_CopyToSaveArea(OSDriver_UserDriverId driverId,
                                       const void *data, uint32_t size)
{
    if (!data || size > kSaveAreaSize) return -1;
    ensureInit();
    mutexLock(&g_lock);
    RegisteredDriver *d = find(driverId);
    if (d) { memcpy(d->saveArea, data, size); d->saveAreaUsed = size; }
    mutexUnlock(&g_lock);
    return d ? 0 : -1;
}

OSDriver_Error OSDriver_CopyFromSaveArea(OSDriver_UserDriverId driverId,
                                         void *data, uint32_t size)
{
    if (!data || size > kSaveAreaSize) return -1;
    ensureInit();
    mutexLock(&g_lock);
    RegisteredDriver *d = find(driverId);
    if (d) memcpy(data, d->saveArea, size);
    mutexUnlock(&g_lock);
    return d ? 0 : -1;
}

void coreinitNxDriverShutdown(void)
{
    ensureInit();
    for (int i = 0; i < kMaxDrivers; i++) {
        mutexLock(&g_lock);
        const bool active = g_drivers[i].used;
        const OSDriver_OnDoneFn onDone = g_drivers[i].iface.onDone;
        const OSDriver_UserDriverId id = g_drivers[i].userDriverId;
        if (active) g_drivers[i].used = false;
        mutexUnlock(&g_lock);

        if (active && onDone) onDone(id);
    }
}

} // extern "C"