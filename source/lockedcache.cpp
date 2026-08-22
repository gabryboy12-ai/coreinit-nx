#include "coreinit/lockedcache.h"

#include <switch.h>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {

// The Espresso can lock 16 KB of its L1 cache and use it as very low
// latency scratch memory, with a DMA engine moving data to and from main
// RAM. ARM64 has no equivalent: you cannot lock L1 from userspace and
// there is no program-controlled DMA.
//
// So we allocate real memory. A stub that allocates nothing would make the
// game read garbage -- it writes data here and reads it back. The latency
// advantage is lost, but it never existed on this hardware anyway.
//
// Aligned to 64 bytes, the A57 cache line, so accesses stay efficient.
//
// decaf-emu reached the same conclusion independently: its
// coreinit_lockedcache.cpp notes "We fake this by performing the load
// immediately".

constexpr uint32_t kLockedCacheSize = 16 * 1024;   // Espresso L1 lock size
constexpr size_t   kAlignment       = 64;          // A57 cache line

std::map<void *, uint32_t> g_blocks;   // puntatore -> dimensione
uint32_t g_allocated = 0;
bool     g_dmaEnabled = true;
::Mutex  g_lock;
bool     g_inited = false;

void ensureInit() {
    if (!g_inited) { mutexInit(&g_lock); g_inited = true; }
}

} // namespace

extern "C" {

// Reporting FALSE would push games onto a fallback path that never ran on
// real hardware -- code the developers themselves barely tested. Reporting
// TRUE keeps them on the well-trodden path, and our allocation satisfies it.
int32_t LCHardwareIsAvailable(void) { return 1; }

void *LCAlloc(uint32_t size)
{
    if (size == 0) return nullptr;
    ensureInit();
    mutexLock(&g_lock);

    if (g_allocated + size > kLockedCacheSize) {
        mutexUnlock(&g_lock);
        return nullptr;    // il budget e' quello vero dell'Espresso
    }

    void *p = aligned_alloc(kAlignment,
                            (size + kAlignment - 1) & ~(kAlignment - 1));
    if (!p) { mutexUnlock(&g_lock); return nullptr; }

    g_blocks[p] = size;
    g_allocated += size;
    mutexUnlock(&g_lock);
    return p;
}

void LCDealloc(void *addr)
{
    if (!addr) return;
    ensureInit();
    mutexLock(&g_lock);
    auto it = g_blocks.find(addr);
    if (it != g_blocks.end()) {
        g_allocated -= it->second;
        g_blocks.erase(it);
        free(addr);
    }
    mutexUnlock(&g_lock);
}

uint32_t LCGetMaxSize(void) { return kLockedCacheSize; }

uint32_t LCGetAllocatableSize(void)
{
    ensureInit();
    mutexLock(&g_lock);
    const uint32_t free_ = kLockedCacheSize - g_allocated;
    mutexUnlock(&g_lock);
    return free_;
}

uint32_t LCGetUnallocated(void) { return LCGetAllocatableSize(); }

int32_t LCIsDMAEnabled(void) { return g_dmaEnabled ? 1 : 0; }
int32_t LCEnableDMA(void)    { g_dmaEnabled = true;  return 1; }
void    LCDisableDMA(void)   { g_dmaEnabled = false; }

// La coda e' sempre vuota: le copie sono gia' finite quando ritornano.
uint32_t LCGetDMAQueueLength(void) { return 0; }

// DMA sincrono. Una coda asincrona vera aggiungerebbe un thread e latenza
// senza alcun beneficio: la memcpy e' gia' istantanea rispetto a quello
// che faceva il DMA hardware dell'Espresso.
void LCLoadDMABlocks(void *dst, const void *src, uint32_t size)
{
    if (dst && src && size) memcpy(dst, src, size);
}

void LCStoreDMABlocks(void *dst, const void *src, uint32_t size)
{
    if (dst && src && size) memcpy(dst, src, size);
}

// Niente da attendere: nessuna operazione resta in volo.
void LCWaitDMAQueue(uint32_t queueLength) { (void)queueLength; }

} // extern "C"