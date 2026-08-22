#include "coreinit/dynload.h"
#include "internal/symbol_registry.hpp"

#include <switch.h>
#include <cstring>
#include <cstdio>

namespace {

// OSDynLoad_Module is void*, so we hand back a pointer to one of these
// instead of inventing an opaque handle.
//
// Games do not load arbitrary plugins through OSDynLoad: they acquire the
// SYSTEM RPLs -- coreinit, gx2, nsysnet -- which are exactly what this
// library implements. Nothing needs loading; the code is already linked in.
// So Acquire is a lookup and FindExport is a table search.
struct LoadedModule {
    char     name[64];
    uint32_t refCount;
    bool     used;
};

constexpr int kMaxModules = 16;
LoadedModule g_modules[kMaxModules];

::Mutex g_lock;
bool    g_inited = false;

OSDynLoadAllocFn g_allocFn = nullptr;
OSDynLoadFreeFn  g_freeFn  = nullptr;

void ensureInit() {
    if (!g_inited) { mutexInit(&g_lock); g_inited = true; }
}

LoadedModule *asModule(OSDynLoad_Module m) {
    auto *p = static_cast<LoadedModule *>(m);
    if (!p) return nullptr;
    if (p < &g_modules[0] || p > &g_modules[kMaxModules - 1]) return nullptr;
    return p->used ? p : nullptr;
}

} // namespace

extern "C" {

OSDynLoad_Error OSDynLoad_Acquire(char const *name,
                                  OSDynLoad_Module *outModule)
{
    if (!outModule) return OS_DYNLOAD_INVALID_ACQUIRE_PTR;
    *outModule = nullptr;
    if (!name) return OS_DYNLOAD_INVALID_MODULE_NAME_PTR;
    if (!name[0]) return OS_DYNLOAD_EMPTY_MODULE_NAME;

    // Cafe OS module names may carry a .rpl suffix; the registry does not.
    char base[64];
    snprintf(base, sizeof(base), "%s", name);
    char *dot = strstr(base, ".rpl");
    if (dot) *dot = '\0';

    if (!coreinit_nx::hasLibrary(base)) {
        // Honest failure beats a handle that resolves nothing later.
        return OS_DYNLOAD_MODULE_NOT_FOUND;
    }

    ensureInit();
    mutexLock(&g_lock);

    for (int i = 0; i < kMaxModules; i++) {
        if (g_modules[i].used && strcmp(g_modules[i].name, base) == 0) {
            g_modules[i].refCount++;      // gia' acquisito: conta i riferimenti
            *outModule = &g_modules[i];
            mutexUnlock(&g_lock);
            return OS_DYNLOAD_OK;
        }
    }

    for (int i = 0; i < kMaxModules; i++) {
        if (!g_modules[i].used) {
            snprintf(g_modules[i].name, sizeof(g_modules[i].name), "%s", base);
            g_modules[i].refCount = 1;
            g_modules[i].used = true;
            *outModule = &g_modules[i];
            mutexUnlock(&g_lock);
            return OS_DYNLOAD_OK;
        }
    }

    mutexUnlock(&g_lock);
    return OS_DYNLOAD_OUT_OF_MEMORY;
}

OSDynLoad_Error OSDynLoad_FindExport(OSDynLoad_Module module,
                                     OSDynLoad_ExportType exportType,
                                     char const *name, void **outAddr)
{
    if (!outAddr) return OS_DYNLOAD_INVALID_ACQUIRE_PTR;
    *outAddr = nullptr;
    if (!name) return OS_DYNLOAD_INVALID_MODULE_NAME_PTR;

    auto *m = asModule(module);
    if (!m) return OS_DYNLOAD_MODULE_NOT_FOUND;

    

    const auto kind = (exportType == OS_DYNLOAD_EXPORT_DATA)
                      ? coreinit_nx::ExportKind::Data
                      : coreinit_nx::ExportKind::Function;
    void *addr = coreinit_nx::findExportOfKind(m->name, name, kind);
    if (!addr) return OS_DYNLOAD_MODULE_NOT_FOUND;

    *outAddr = addr;
    return OS_DYNLOAD_OK;
}

void OSDynLoad_Release(OSDynLoad_Module module)
{
    ensureInit();
    mutexLock(&g_lock);
    auto *m = asModule(module);
    if (m && m->refCount > 0 && --m->refCount == 0) {
        m->used = false;   // nulla da scaricare: il codice resta linkato
    }
    mutexUnlock(&g_lock);
}

// Cafe OS uses these so the game can supply memory for loaded modules.
// Nothing is ever loaded here, so they are stored and never called.
OSDynLoad_Error OSDynLoad_SetAllocator(OSDynLoadAllocFn allocFn,
                                       OSDynLoadFreeFn freeFn)
{
    if (!allocFn || !freeFn) return OS_DYNLOAD_INVALID_ALLOCATOR_PTR;
    g_allocFn = allocFn;
    g_freeFn  = freeFn;
    return OS_DYNLOAD_OK;
}

OSDynLoad_Error OSDynLoad_GetAllocator(OSDynLoadAllocFn *outAllocFn,
                                       OSDynLoadFreeFn *outFreeFn)
{
    if (!outAllocFn || !outFreeFn) return OS_DYNLOAD_INVALID_ALLOCATOR_PTR;
    *outAllocFn = g_allocFn;
    *outFreeFn  = g_freeFn;
    return OS_DYNLOAD_OK;
}

// wut: "Always returns 0 on release versions of CafeOS. Requires
// OSGetSecurityLevel() > 0." These are debug-build tools that never worked
// on a retail console, so any game calling them already has a fallback.
int32_t OSDynLoad_GetNumberOfRPLs(void) { return 0; }

int32_t OSDynLoad_GetRPLInfo(uint32_t first, uint32_t count, void *outInfo)
{
    (void)first; (void)count; (void)outInfo;
    return 0;
}

} // extern "C"