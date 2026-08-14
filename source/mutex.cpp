#include "coreinit/mutex.h"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {

// I mutex di Cafe OS sono RICORSIVI: lo stesso thread puo' acquisirli
// piu' volte senza bloccarsi, e deve rilasciarli altrettante volte.
//
// libnx Mutex NON e' ricorsivo. RMutex si'. Usare RMutex.
//
// Sbagliare qui non esplode subito: torna indietro come deadlock
// intermittenti settimane dopo, quando il gioco entra in un percorso
// di codice che riacquisisce lo stesso lock.
struct HostMutex {
    RMutex handle;
    void init() { rmutexInit(&handle); }
};

coreinit_nx::HandleTable<HostMutex> g_mutexes;

} // namespace

extern "C" {

void OSInitMutex(OSMutex *mutex)
{
    g_mutexes.get(mutex);
}

void OSInitMutexEx(OSMutex *mutex, const char *name)
{
    // Il nome serve solo al debugger su hardware reale: lo ignoriamo.
    (void)name;
    g_mutexes.get(mutex);
}

void OSLockMutex(OSMutex *mutex)
{
    rmutexLock(&g_mutexes.get(mutex)->handle);
}

void OSUnlockMutex(OSMutex *mutex)
{
    rmutexUnlock(&g_mutexes.get(mutex)->handle);
}

int32_t OSTryLockMutex(OSMutex *mutex)
{
    // Cafe OS restituisce 1 se acquisito, 0 altrimenti.
    // VERIFICARE in decaf-emu: libdecaf/src/cafe/libraries/coreinit/
    return rmutexTryLock(&g_mutexes.get(mutex)->handle) ? 1 : 0;
}

} // extern "C"
