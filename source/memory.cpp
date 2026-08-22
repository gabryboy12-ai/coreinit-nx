#include "coreinit/memory.h"

#include <switch.h>
#include <cstring>

extern "C" {

// Il flag 'flush' chiede di sincronizzare la cache dopo la copia: sul Wii U
// serviva perche' la GPU leggeva la RAM direttamente.
void *OSBlockMove(void *dst, const void *src, uint32_t size, int32_t flush)
{
    if (!dst || !src || !size) return dst;
    memmove(dst, src, size);       // move, non copy: le regioni possono sovrapporsi
    if (flush) armDCacheFlush(dst, size);
    return dst;
}

void *OSBlockSet(void *dst, uint8_t val, uint32_t size)
{
    if (!dst || !size) return dst;
    memset(dst, val, size);
    return dst;
}

} // extern "C"