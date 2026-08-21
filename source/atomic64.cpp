#include "coreinit/atomic64.h"

// Compiler intrinsics, not locks: GCC emits native ARM64 LSE or
// load-exclusive/store-exclusive sequences directly. This is the fastest
// implementation available -- wrapping these in a mutex would be an order
// of magnitude slower and would deadlock if a game used them from an
// interrupt-like context.
//
// __ATOMIC_SEQ_CST matches the PowerPC sync/lwarx-stwcx pairs Cafe OS uses,
// which are full barriers.

extern "C" {

uint64_t OSGetAtomic64(volatile uint64_t *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

uint64_t OSSetAtomic64(volatile uint64_t *ptr, uint64_t value)
{
    // Cafe OS returns the PREVIOUS value, so this is an exchange.
    return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t OSCompareAndSwapAtomic64(volatile uint64_t *ptr, uint64_t compare,
                                 uint64_t value)
{
    uint64_t expected = compare;
    return __atomic_compare_exchange_n(ptr, &expected, value, false,
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST) ? 1 : 0;
}

uint64_t OSSwapAtomic64(volatile uint64_t *ptr, uint64_t value)
{
    return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
}

int64_t OSAddAtomic64(volatile int64_t *ptr, int64_t value)
{
    // Returns the NEW value on Cafe OS -- fetch_add would return the old.
    return __atomic_add_fetch(ptr, value, __ATOMIC_SEQ_CST);
}

uint64_t OSAndAtomic64(volatile uint64_t *ptr, uint64_t value)
{
    return __atomic_and_fetch(ptr, value, __ATOMIC_SEQ_CST);
}

uint64_t OSOrAtomic64(volatile uint64_t *ptr, uint64_t value)
{
    return __atomic_or_fetch(ptr, value, __ATOMIC_SEQ_CST);
}

uint64_t OSXorAtomic64(volatile uint64_t *ptr, uint64_t value)
{
    return __atomic_xor_fetch(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t OSTestAndClearAtomic64(volatile uint64_t *ptr, uint32_t bit)
{
    const uint64_t mask = 1ull << (bit & 63);
    const uint64_t previous = __atomic_fetch_and(ptr, ~mask, __ATOMIC_SEQ_CST);
    return (previous & mask) ? 1 : 0;   // esito: com'era il bit PRIMA
}

int32_t OSTestAndSetAtomic64(volatile uint64_t *ptr, uint32_t bit)
{
    const uint64_t mask = 1ull << (bit & 63);
    const uint64_t previous = __atomic_fetch_or(ptr, mask, __ATOMIC_SEQ_CST);
    return (previous & mask) ? 1 : 0;
}

} // extern "C"