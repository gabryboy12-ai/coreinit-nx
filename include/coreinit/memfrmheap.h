#pragma once
#include <stdint.h>
#include "coreinit/memheap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MEMFrmHeapFreeMode {
    MEM_FRM_HEAP_FREE_HEAD = 1 << 0,
    MEM_FRM_HEAP_FREE_TAIL = 1 << 1,
    MEM_FRM_HEAP_FREE_ALL  = MEM_FRM_HEAP_FREE_HEAD | MEM_FRM_HEAP_FREE_TAIL,
} MEMFrmHeapFreeMode;

// Da wut: WUT_CHECK_SIZE(MEMFrmHeap, 0x4C)
#define COREINIT_NX_MEMFRMHEAP_SIZE 0x4C

MEMHeapHandle MEMCreateFrmHeapEx(void *heap, uint32_t size, uint32_t flags);
void         *MEMDestroyFrmHeap(MEMHeapHandle heap);
void         *MEMAllocFromFrmHeapEx(MEMHeapHandle heap, uint32_t size,
                                    int alignment);
void          MEMFreeToFrmHeap(MEMHeapHandle heap, MEMFrmHeapFreeMode mode);
int32_t       MEMRecordStateForFrmHeap(MEMHeapHandle heap, uint32_t tag);
int32_t       MEMFreeByStateToFrmHeap(MEMHeapHandle heap, uint32_t tag);
uint32_t      MEMAdjustFrmHeap(MEMHeapHandle heap);
uint32_t      MEMGetAllocatableSizeForFrmHeapEx(MEMHeapHandle heap,
                                                int alignment);

#ifdef __cplusplus
}
#endif