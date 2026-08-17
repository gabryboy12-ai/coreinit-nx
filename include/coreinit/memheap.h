#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: WUT_CHECK_SIZE(MEMHeapHeader, 0x40)
typedef struct MEMHeapHeader {
    uint8_t _opaque[0x40];
} MEMHeapHeader;

typedef MEMHeapHeader *MEMHeapHandle;

typedef enum MEMHeapTag {
    MEM_BLOCK_HEAP_TAG    = 0x424C4B48u,
    MEM_EXPANDED_HEAP_TAG = 0x45585048u,
    MEM_FRAME_HEAP_TAG    = 0x46524D48u,
    MEM_UNIT_HEAP_TAG     = 0x554E5448u,
    MEM_USER_HEAP_TAG     = 0x55535248u,
} MEMHeapTag;

typedef enum MEMHeapFlags {
    MEM_HEAP_FLAG_ZERO_ALLOCATED = 1 << 0,
    MEM_HEAP_FLAG_DEBUG_MODE     = 1 << 1,
    MEM_HEAP_FLAG_USE_LOCK       = 1 << 2,
} MEMHeapFlags;

#ifdef __cplusplus
}
#endif