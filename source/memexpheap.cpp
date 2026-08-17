#include "coreinit/memexpheap.h"
#include "internal/handle_table.hpp"

#include <switch.h>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace {

// Expanded heap: a general allocator over a guest-supplied region.
//
// Cafe OS puts a 0x14-byte MEMExpHeapBlock header immediately before every
// allocation, inside the region. We keep the bookkeeping host-side, but we
// CHARGE THE SAME 0x14 OVERHEAD anyway -- otherwise MEMGetTotalFreeSizeFor
// ExpHeap would report more free space than Cafe OS would, and a game that
// allocates until failure would behave differently. Over-reporting memory
// fails far away from its cause.
//
// The reserved header bytes are left untouched: writing them little-endian
// when recompiled code reads big-endian would be worse than not writing.
struct Block {
    uintptr_t start;        // includes the reserved header
    uint32_t  size;         // header + padding + payload
    uintptr_t payload;      // the pointer handed to the caller
    uint32_t  payloadSize;
    bool      used;
    uint8_t   direction;
};

struct HostExpHeap {
    uintptr_t dataStart;
    uintptr_t dataEnd;
    uint16_t  flags;
    uint8_t   mode;
    bool      live;
    std::vector<Block> blocks;   // sorted by start, covers the whole region

    void init() {
        dataStart = dataEnd = 0;
        flags = 0;
        mode = MEM_EXP_HEAP_MODE_FIRST_FREE;
        live = false;
        blocks.clear();
    }
};

coreinit_nx::HandleTable<HostExpHeap> g_expHeaps;

bool isPow2(uint32_t v) { return v && !(v & (v - 1)); }
uintptr_t alignUp(uintptr_t v, uint32_t a)   { return (v + a - 1) & ~(uintptr_t)(a - 1); }
uintptr_t alignDown(uintptr_t v, uint32_t a) { return v & ~(uintptr_t)(a - 1); }

// Same Revolution-SDK convention as the frame heap: negative alignment
// means allocate from the bottom (tail) of the region.
uint32_t normaliseAlignment(int alignment, bool *fromBottom) {
    *fromBottom = alignment < 0;
    uint32_t a = (uint32_t)(alignment < 0 ? -alignment : alignment);
    if (!isPow2(a)) a = 4;
    return a;
}

HostExpHeap *live(MEMHeapHandle heap) {
    if (!heap) return nullptr;
    auto *h = g_expHeaps.get(heap);
    return h->live ? h : nullptr;
}

// Largest payload of the given alignment that fits in this free block.
// Identical in both directions: whether the payload sits at the low or the
// high end, the constraint is the same -- the 0x14 header must fit before
// it and the payload must be aligned.
uint32_t fitInFree(const Block &b, uint32_t a) {
    const uintptr_t end = b.start + b.size;
    const uintptr_t lowest = alignUp(b.start + COREINIT_NX_EXPBLOCK_SIZE, a);
    if (lowest >= end) return 0;
    return (uint32_t)(end - lowest);
}

void coalesce(HostExpHeap *h) {
    for (size_t i = 0; i + 1 < h->blocks.size(); ) {
        if (!h->blocks[i].used && !h->blocks[i + 1].used) {
            h->blocks[i].size += h->blocks[i + 1].size;
            h->blocks.erase(h->blocks.begin() + (long)i + 1);
        } else {
            i++;
        }
    }
}

} // namespace

extern "C" {

MEMHeapHandle MEMCreateExpHeapEx(void *heap, uint32_t size, uint16_t flags)
{
    if (!heap || size <= COREINIT_NX_MEMEXPHEAP_SIZE + COREINIT_NX_EXPBLOCK_SIZE)
        return nullptr;

    auto *h = g_expHeaps.get(heap);
    const uintptr_t base = (uintptr_t)heap;

    h->dataStart = alignUp(base + COREINIT_NX_MEMEXPHEAP_SIZE, 4);
    h->dataEnd   = base + size;
    if (h->dataStart >= h->dataEnd) return nullptr;

    h->flags = flags;
    h->mode  = MEM_EXP_HEAP_MODE_FIRST_FREE;
    h->live  = true;
    h->blocks.clear();
    h->blocks.push_back(Block{h->dataStart,
                              (uint32_t)(h->dataEnd - h->dataStart),
                              0, 0, false, MEM_EXP_HEAP_DIR_FROM_TOP});
    return (MEMHeapHandle)heap;
}

void *MEMDestroyExpHeap(MEMHeapHandle heap)
{
    auto *h = live(heap);
    if (!h) return nullptr;
    h->live = false;
    h->blocks.clear();
    return (void *)heap;
}

void *MEMAllocFromExpHeapEx(MEMHeapHandle heap, uint32_t size, int alignment)
{
    auto *h = live(heap);
    if (!h) return nullptr;
    if (size == 0) size = 1;

    bool fromBottom = false;
    const uint32_t a = normaliseAlignment(alignment, &fromBottom);

    // Pick a free block: first that fits, or the tightest fit.
    size_t chosen = (size_t)-1;
    uint32_t chosenSlack = 0;
    for (size_t i = 0; i < h->blocks.size(); i++) {
        if (h->blocks[i].used) continue;
        const uint32_t avail = fitInFree(h->blocks[i], a);
        if (avail < size) continue;
        if (h->mode == MEM_EXP_HEAP_MODE_FIRST_FREE) { chosen = i; break; }
        const uint32_t slack = avail - size;
        if (chosen == (size_t)-1 || slack < chosenSlack) {
            chosen = i; chosenSlack = slack;
        }
    }
    if (chosen == (size_t)-1) return nullptr;

    Block free = h->blocks[chosen];
    const uintptr_t freeEnd = free.start + free.size;

    uintptr_t payload, blockStart, blockEnd;
    if (fromBottom) {
        payload    = alignDown(freeEnd - size, a);
        blockStart = payload - COREINIT_NX_EXPBLOCK_SIZE;
        blockEnd   = freeEnd;
        if (blockStart < free.start) return nullptr;
    } else {
        payload    = alignUp(free.start + COREINIT_NX_EXPBLOCK_SIZE, a);
        blockStart = free.start;
        blockEnd   = payload + size;
        if (blockEnd > freeEnd) return nullptr;
    }

    Block used{blockStart, (uint32_t)(blockEnd - blockStart),
               payload, size, true,
               (uint8_t)(fromBottom ? MEM_EXP_HEAP_DIR_FROM_BOTTOM
                                    : MEM_EXP_HEAP_DIR_FROM_TOP)};

    // Split off whatever is left, unless the remainder is too small to
    // ever hold a block of its own.
    std::vector<Block> replacement;
    const uint32_t minSplit = COREINIT_NX_EXPBLOCK_SIZE + 4;

    if (fromBottom) {
        const uint32_t before = (uint32_t)(blockStart - free.start);
        if (before >= minSplit) {
            replacement.push_back(Block{free.start, before, 0, 0, false,
                                        MEM_EXP_HEAP_DIR_FROM_TOP});
        } else if (before > 0) {
            used.start = free.start;
            used.size += before;
        }
        replacement.push_back(used);
    } else {
        const uint32_t after = (uint32_t)(freeEnd - blockEnd);
        if (after >= minSplit) {
            replacement.push_back(used);
            replacement.push_back(Block{blockEnd, after, 0, 0, false,
                                        MEM_EXP_HEAP_DIR_FROM_TOP});
        } else {
            used.size += after;
            replacement.push_back(used);
        }
    }

    h->blocks.erase(h->blocks.begin() + (long)chosen);
    h->blocks.insert(h->blocks.begin() + (long)chosen,
                     replacement.begin(), replacement.end());

    if (h->flags & MEM_HEAP_FLAG_ZERO_ALLOCATED) {
        std::memset((void *)payload, 0, size);
    }
    return (void *)payload;
}

void MEMFreeToExpHeap(MEMHeapHandle heap, void *block)
{
    auto *h = live(heap);
    if (!h || !block) return;

    for (auto &b : h->blocks) {
        if (b.used && b.payload == (uintptr_t)block) {
            b.used = false;
            b.payload = 0;
            b.payloadSize = 0;
            coalesce(h);
            return;
        }
    }
    // Freeing an unknown pointer is a guest bug; ignore rather than crash.
}

uint32_t MEMGetTotalFreeSizeForExpHeap(MEMHeapHandle heap)
{
    auto *h = live(heap);
    if (!h) return 0;
    uint32_t total = 0;
    for (const auto &b : h->blocks) if (!b.used) total += b.size;
    return total;
}

uint32_t MEMGetAllocatableSizeForExpHeapEx(MEMHeapHandle heap, int alignment)
{
    auto *h = live(heap);
    if (!h) return 0;

    bool fromBottom = false;
    const uint32_t a = normaliseAlignment(alignment, &fromBottom);
    (void)fromBottom;   // la direzione non cambia lo spazio disponibile

    uint32_t best = 0;
    for (const auto &b : h->blocks) {
        if (b.used) continue;
        const uint32_t avail = fitInFree(b, a);
        if (avail > best) best = avail;
    }
    return best;
}

MEMExpHeapMode MEMSetAllocModeForExpHeap(MEMHeapHandle heap,
                                         MEMExpHeapMode mode)
{
    auto *h = live(heap);
    if (!h) return MEM_EXP_HEAP_MODE_FIRST_FREE;
    const MEMExpHeapMode previous = (MEMExpHeapMode)h->mode;
    h->mode = (uint8_t)mode;
    return previous;
}

MEMExpHeapMode MEMGetAllocModeForExpHeap(MEMHeapHandle heap)
{
    auto *h = live(heap);
    return h ? (MEMExpHeapMode)h->mode : MEM_EXP_HEAP_MODE_FIRST_FREE;
}

// ---------------------------------------------------------------
// Base heaps
//
// On Cafe OS these exist before the game starts. Here nothing has created
// them, so we build one lazily on host memory. The 8 MB size is arbitrary
// and should become configurable, set by the port.
// ---------------------------------------------------------------

static void         *g_baseRegion[3] = { nullptr, nullptr, nullptr };
static MEMHeapHandle g_baseHeap[3]   = { nullptr, nullptr, nullptr };

static int baseIndex(MEMBaseHeapType type)
{
    switch (type) {
        case MEM_BASE_HEAP_MEM1: return 0;
        case MEM_BASE_HEAP_MEM2: return 1;
        case MEM_BASE_HEAP_FG:   return 2;
        default:                 return -1;
    }
}

MEMHeapHandle MEMGetBaseHeapHandle(MEMBaseHeapType type)
{
    const int i = baseIndex(type);
    if (i < 0) return nullptr;
    if (g_baseHeap[i]) return g_baseHeap[i];

    const uint32_t kSize = 8u * 1024u * 1024u;
    g_baseRegion[i] = std::malloc(kSize);
    if (!g_baseRegion[i]) return nullptr;

    g_baseHeap[i] = MEMCreateExpHeapEx(g_baseRegion[i], kSize, 0);
    return g_baseHeap[i];
}

MEMHeapHandle MEMSetBaseHeapHandle(MEMBaseHeapType type, MEMHeapHandle heap)
{
    const int i = baseIndex(type);
    if (i < 0) return nullptr;
    MEMHeapHandle previous = g_baseHeap[i];
    g_baseHeap[i] = heap;
    return previous;
}

} // extern "C"