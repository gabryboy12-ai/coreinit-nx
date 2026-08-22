#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*MEMAllocFromDefaultHeapFn)(uint32_t size);
typedef void *(*MEMAllocFromDefaultHeapExFn)(uint32_t size, int32_t alignment);
typedef void  (*MEMFreeToDefaultHeapFn)(void *ptr);

// Queste sono VARIABILI, non funzioni: il gioco legge il puntatore e chiama
// attraverso di esso, e puo' anche sostituirlo per intercettare le
// allocazioni -- pattern comune nei motori commerciali.
extern MEMAllocFromDefaultHeapFn   MEMAllocFromDefaultHeap;
extern MEMAllocFromDefaultHeapExFn MEMAllocFromDefaultHeapEx;
extern MEMFreeToDefaultHeapFn      MEMFreeToDefaultHeap;

#ifdef __cplusplus
}
#endif