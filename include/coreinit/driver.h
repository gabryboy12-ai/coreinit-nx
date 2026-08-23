#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t OSDriver_UserDriverId;
typedef uint32_t OSDynLoad_ModuleHandle;
typedef int32_t  OSDriver_Error;

typedef const char *(*OSDriver_GetNameFn)(OSDriver_UserDriverId);
typedef void (*OSDriver_OnInitFn)(OSDriver_UserDriverId);
typedef void (*OSDriver_OnAcquiredForegroundFn)(OSDriver_UserDriverId);
typedef void (*OSDriver_OnReleasedForegroundFn)(OSDriver_UserDriverId);
typedef void (*OSDriver_OnDoneFn)(OSDriver_UserDriverId);

#pragma pack(push, 1)
typedef struct OSDriverInterface {
    OSDriver_GetNameFn              getName;
    OSDriver_OnInitFn               onInit;
    OSDriver_OnAcquiredForegroundFn onAcquiredForeground;
    OSDriver_OnReleasedForegroundFn onReleasedForeground;
    OSDriver_OnDoneFn               onDone;
} OSDriverInterface;
#pragma pack(pop)

OSDriver_Error OSDriver_Register(OSDynLoad_ModuleHandle moduleHandle,
                                 uint32_t unk1,
                                 OSDriverInterface *driverInterface,
                                 OSDriver_UserDriverId userDriverId,
                                 uint32_t *outRegisteredUpid,
                                 uint32_t *outOwnerUpid,
                                 int32_t *outDidOSDriverInit);
OSDriver_Error OSDriver_Deregister(OSDynLoad_ModuleHandle moduleHandle,
                                   OSDriver_UserDriverId userDriverId);
OSDriver_Error OSDriver_CopyFromSaveArea(OSDriver_UserDriverId driverId,
                                         void *data, uint32_t size);
OSDriver_Error OSDriver_CopyToSaveArea(OSDriver_UserDriverId driverId,
                                       const void *data, uint32_t size);

// --- estensione coreinit-nx ---
// Invoca onDone su tutti i driver registrati. Un port deve chiamarla in
// chiusura: su Cafe OS lo faceva il sistema.
void coreinitNxDriverShutdown(void);

#ifdef __cplusplus
}
#endif