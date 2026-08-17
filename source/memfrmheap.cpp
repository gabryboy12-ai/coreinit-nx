#include "coreinit/memfrmheap.h"
#include "internal/handle_table.hpp"

#include <switch.h>
#include <cstring>
#include <vector>

namespace {

// Frame heap: a two-ended stack. Allocation moves 'head' up from the start
// or 'tail' down from the end. Freeing resets one or both to their limits,
// or back to a recorded state. No free list, no coalescing.
//
// DEVIATION FROM CAFE OS: state records are kept host-side rather than
// allocated from the heap head. Cafe OS spends 0x10 bytes of heap per
// recorded state; we spend none, so our reported free space is slightly
// larger after MEMRecordStateForFrmHeap.
struct State {
    uint32_t  tag;
    uintptr_t head;
    uintptr_t tail;
};

struct HostFrmHeap {
    uintptr_t dataStart;
    uintptr_t dataEnd;
    uintptr_t head;
    uintptr_t tail;
    uint32_t  flags;
    bool      live;
    std::vector<State> states;

    void init() {
        dataStart = dataEnd = head = tail = 0;
        flags = 0;
        live = false;
        states.clear();
    }
};

coreinit_nx::HandleTable<HostFrmHeap> g_frmHeaps;

bool isPow2(uint32_t v) { return v && !(v & (v - 1)); }

uintptr_t alignUp(uintptr_t v, uint32_t a) {
    return (v + a - 1) & ~(uintptr_t)(a - 1);
}

uintptr_t alignDown(uintptr_t v, uint32_t a) {
    return v & ~(uintptr_t)(a - 1);
}

// Cafe OS convention inherited from the Revolution SDK: a NEGATIVE
// alignment means "allocate from the tail". Getting this wrong yields an
// allocator that works until the game asks for memory from the end.
// ASSUMPTION -- confirm against decaf-emu.
uint32_t normaliseAlignment(int alignment, bool *fromTail) {
    *fromTail = alignment < 0;
    uint32_t a = (uint32_t)(alignment < 0 ? -alignment : alignment);
    if (!isPow2(a)) a = 4;
    return a;
}

HostFrmHeap *live(MEMHeapHandle heap) {
    if (!heap) return nullptr;
    auto *h = g_frmHeaps.get(heap);
    return h->live ? h : nullptr;
}

} // namespace

extern "C" {

MEMHeapHandle MEMCreateFrmHeapEx(void *heap, uint32_t size, uint32_t flags)
{
    if (!heap || size <= COREINIT_NX_MEMFRMHEAP_SIZE) return nullptr;

    auto *h = g_frmHeaps.get(heap);
    const uintptr_t base = (uintptr_t)heap;

    // The MEMFrmHeap structure itself lives at the start of the region.
    // We do not populate it (see README: known limitations), but we must
    // reserve it so that reported free space matches Cafe OS.
    h->dataStart = alignUp(base + COREINIT_NX_MEMFRMHEAP_SIZE, 4);
    h->dataEnd   = base + size;
    if (h->dataStart >= h->dataEnd) return nullptr;

    h->head  = h->dataStart;
    h->tail  = h->dataEnd;
    h->flags = flags;
    h->live  = true;
    h->states.clear();

    return (MEMHeapHandle)heap;
}

void *MEMDestroyFrmHeap(MEMHeapHandle heap)
{
    auto *h = live(heap);
    if (!h) return nullptr;
    h->live = false;
    h->states.clear();
    return (void *)heap;
}

void *MEMAllocFromFrmHeapEx(MEMHeapHandle heap, uint32_t size, int alignment)
{
    auto *h = live(heap);
    if (!h) return nullptr;
    if (size == 0) size = 1;

    bool fromTail = false;
    const uint32_t a = normaliseAlignment(alignment, &fromTail);

    uintptr_t result;
    if (fromTail) {
        const uintptr_t candidate = alignDown(h->tail - size, a);
        if (candidate < h->head || candidate > h->tail) return nullptr;
        h->tail = candidate;
        result  = candidate;
    } else {
        const uintptr_t candidate = alignUp(h->head, a);
        if (candidate + size > h->tail || candidate < h->head) return nullptr;
        h->head = candidate + size;
        result  = candidate;
    }

    if (h->flags & MEM_HEAP_FLAG_ZERO_ALLOCATED) {
        std::memset((void *)result, 0, size);
    }
    return (void *)result;
}

void MEMFreeToFrmHeap(MEMHeapHandle heap, MEMFrmHeapFreeMode mode)
{
    auto *h = live(heap);
    if (!h) return;

    if (mode & MEM_FRM_HEAP_FREE_HEAD) h->head = h->dataStart;
    if (mode & MEM_FRM_HEAP_FREE_TAIL) h->tail = h->dataEnd;

    // Recorded states referring to freed regions are no longer meaningful.
    if ((mode & MEM_FRM_HEAP_FREE_ALL) == MEM_FRM_HEAP_FREE_ALL) {
        h->states.clear();
    }
}

int32_t MEMRecordStateForFrmHeap(MEMHeapHandle heap, uint32_t tag)
{
    auto *h = live(heap);
    if (!h) return 0;
    h->states.push_back(State{tag, h->head, h->tail});
    return 1;
}

int32_t MEMFreeByStateToFrmHeap(MEMHeapHandle heap, uint32_t tag)
{
    auto *h = live(heap);
    if (!h || h->states.empty()) return 0;

    size_t idx;
    if (tag == 0) {
        idx = h->states.size() - 1;      // tag 0: most recent state
    } else {
        bool found = false;
        for (size_t i = h->states.size(); i-- > 0; ) {
            if (h->states[i].tag == tag) { idx = i; found = true; break; }
        }
        if (!found) return 0;
    }

    h->head = h->states[idx].head;
    h->tail = h->states[idx].tail;
    h->states.resize(idx);               // drop it and everything above
    return 1;
}

uint32_t MEMAdjustFrmHeap(MEMHeapHandle heap)
{
    auto *h = live(heap);
    if (!h) return 0;
    // Only meaningful when nothing was allocated from the tail: shrink the
    // heap to end at the current head.
    if (h->tail != h->dataEnd) return 0;
    h->dataEnd = h->head;
    h->tail    = h->head;
    return (uint32_t)(h->dataEnd - (uintptr_t)heap);
}

uint32_t MEMGetAllocatableSizeForFrmHeapEx(MEMHeapHandle heap, int alignment)
{
    auto *h = live(heap);
    if (!h) return 0;

    bool fromTail = false;
    const uint32_t a = normaliseAlignment(alignment, &fromTail);

    const uintptr_t start = alignUp(h->head, a);
    if (start >= h->tail) return 0;
    return (uint32_t)(h->tail - start);
}

} // extern "C"