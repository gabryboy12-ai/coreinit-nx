#include "coreinit/memdefaultheap.h"
#include "coreinit/memexpheap.h"
#include "coreinit/memheap.h"

#include <switch.h>

namespace {

// Cafe OS initialises these pointers at startup; a game may then replace
// them to hook allocation. We do the same: point them at implementations
// backed by the MEM2 base heap.
void *defaultAlloc(uint32_t size)
{
    return MEMAllocFromExpHeapEx(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2),
                                 size, 4);
}

void *defaultAllocEx(uint32_t size, int32_t alignment)
{
    return MEMAllocFromExpHeapEx(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2),
                                 size, alignment ? alignment : 4);
}

void defaultFree(void *ptr)
{
    MEMFreeToExpHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2), ptr);
}

} // namespace

extern "C" {

MEMAllocFromDefaultHeapFn   MEMAllocFromDefaultHeap   = defaultAlloc;
MEMAllocFromDefaultHeapExFn MEMAllocFromDefaultHeapEx = defaultAllocEx;
MEMFreeToDefaultHeapFn      MEMFreeToDefaultHeap      = defaultFree;

} // extern "C"