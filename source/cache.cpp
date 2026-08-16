#include "coreinit/cache.h"

#include <switch.h>

// Manutenzione della cache PowerPC mappata sugli equivalenti ARM64.
//
//   DCFlushRange = clean + invalidate -> armDCacheFlush
//   DCStoreRange = clean soltanto     -> armDCacheClean
//
// CAVEAT: sul Wii U queste contano perche' la GPU legge direttamente la
// memoria principale e la cache della CPU non e' coerente con essa. Sotto
// Horizon la situazione e' diversa, e per il codice ricompilato potrebbero
// finire per essere piu' vicine a delle no-op che a vere operazioni di
// cache. Mappate alla lettera per ora; da rivedere quando ci sara' codice
// ricompilato che le esercita davvero.

extern "C" {

void DCFlushRange(void *addr, uint32_t size)
{
    if (addr && size) armDCacheFlush(addr, size);
}

void DCStoreRange(void *addr, uint32_t size)
{
    if (addr && size) armDCacheClean(addr, size);
}

} // extern "C"