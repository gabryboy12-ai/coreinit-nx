#pragma once
#include <stdint.h>
#include "coreinit/memheap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MEMExpHeapMode {
    MEM_EXP_HEAP_MODE_FIRST_FREE   = 0,
    MEM_EXP_HEAP_MODE_NEAREST_SIZE = 1,
} MEMExpHeapMode;

typedef enum MEMExpHeapDirection {
    MEM_EXP_HEAP_DIR_FROM_TOP    = 0,
    MEM_EXP_HEAP_DIR_FROM_BOTTOM = 1,
} MEMExpHeapDirection;

// Da wut: MEMExpHeap 0x54, MEMExpHeapBlock 0x14
#define COREINIT_NX_MEMEXPHEAP_SIZE  0x54
#define COREINIT_NX_EXPBLOCK_SIZE    0x14

MEMHeapHandle  MEMCreateExpHeapEx(void *heap, uint32_t size, uint16_t flags);
void          *MEMDestroyExpHeap(MEMHeapHandle heap);
void          *MEMAllocFromExpHeapEx(MEMHeapHandle heap, uint32_t size,
                                     int alignment);
void           MEMFreeToExpHeap(MEMHeapHandle heap, void *block);
uint32_t       MEMGetTotalFreeSizeForExpHeap(MEMHeapHandle heap);
uint32_t       MEMGetAllocatableSizeForExpHeapEx(MEMHeapHandle heap,
                                                 int alignment);
MEMExpHeapMode MEMSetAllocModeForExpHeap(MEMHeapHandle heap,
                                         MEMExpHeapMode mode);
MEMExpHeapMode MEMGetAllocModeForExpHeap(MEMHeapHandle heap);

#ifdef __cplusplus
}
#endif