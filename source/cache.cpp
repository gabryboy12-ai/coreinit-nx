#include "coreinit/cache.h"

#include <switch.h>
#include <cstring>

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

// L'Espresso lavora su linee da 32 byte e wut documenta che la dimensione
// viene arrotondata al successivo 0x20. Un gioco puo' contarci: se azzera
// 1 byte si aspetta che i 32 della linea siano azzerati.
static inline uint32_t roundToCacheLine(uint32_t size)
{
    return (size + 0x1F) & ~0x1Fu;
}

void DCInvalidateRange(void *addr, uint32_t size)
{
    // Invalidate senza clean scarterebbe dati non ancora scritti in RAM.
    // ARM64 non espone un invalidate puro da userspace, e comunque su
    // Horizon non c'e' hardware che scriva la memoria alle nostre spalle:
    // flush e' la scelta sicura.
    if (addr && size) armDCacheFlush(addr, roundToCacheLine(size));
}

void DCZeroRange(void *addr, uint32_t size)
{
    if (!addr || !size) return;
    const uint32_t rounded = roundToCacheLine(size);
    memset(addr, 0, rounded);
    armDCacheFlush(addr, rounded);
}

void DCTouchRange(void *addr, uint32_t size)
{
    // Prefetch: su PowerPC portava le linee in cache senza leggerle dalla
    // RAM. Qui e' un suggerimento, non un obbligo. Lo lasciamo no-op:
    // fingere un prefetch con letture finte sprecherebbe banda.
    (void)addr; (void)size;
}

void ICInvalidateRange(void *addr, uint32_t size)
{
    if (addr && size) armICacheInvalidate(addr, roundToCacheLine(size));
}

} // extern "C"