#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ATTENZIONE — VALORE DA VERIFICARE
//
// Questa dimensione deve corrispondere esattamente a quella di
// OSMutex in Cafe OS, perche' e' il codice del gioco ad allocare
// la struttura, non noi.
//
// Fonte autoritativa: wut/include/coreinit/mutex.h
// Cerca la riga:  WUT_CHECK_SIZE(OSMutex,
// (con la virgola, altrimenti trovi OSMutexLink che e' un'altra cosa)
//
// Se il valore reale e' diverso da 0x2C, correggilo qui.
// ============================================================
#define COREINIT_NX_OSMUTEX_SIZE 0x2C

// Trattiamo la struttura come opaca: non leggiamo mai il suo
// contenuto. Ci serve solo che occupi lo spazio giusto, perche'
// il gioco puo' incorporarla dentro strutture proprie.
// Il suo INDIRIZZO fa da chiave verso il vero mutex lato host.
typedef struct OSMutex {
    uint8_t _opaque[COREINIT_NX_OSMUTEX_SIZE];
} OSMutex;

void    OSInitMutex(OSMutex *mutex);
void    OSInitMutexEx(OSMutex *mutex, const char *name);
void    OSLockMutex(OSMutex *mutex);
void    OSUnlockMutex(OSMutex *mutex);
int32_t OSTryLockMutex(OSMutex *mutex);

#ifdef __cplusplus
}
#endif
