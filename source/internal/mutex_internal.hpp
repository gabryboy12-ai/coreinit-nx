#pragma once
#include <switch.h>
#include <stdint.h>

#include "coreinit/mutex.h"

namespace coreinit_nx {

// Rappresentazione host di un OSMutex.
//
// PUNTO CHIAVE: la Mutex sottostante viene bloccata UNA VOLTA SOLA,
// qualunque sia la profondita' di ricorsione. La ricorsivita' la
// tracciamo noi in 'count', non il kernel.
//
// E' cio' che rende corretto OSWaitCond: condvarWait rilascia la Mutex
// esattamente una volta, che e' quante volte l'abbiamo acquisita davvero.
//
// 'count' e' letto e scritto solo dal thread che possiede il lock,
// quindi non serve protezione aggiuntiva.
struct HostMutex {
    ::Mutex lock;
    uint32_t count;

    void init() {
        mutexInit(&lock);
        count = 0;
    }
};

// Esposto al modulo delle condition variable, che deve salvare e
// ripristinare la profondita' attorno all'attesa.
HostMutex *getHostMutex(const OSMutex *mutex);

} // namespace coreinit_nx