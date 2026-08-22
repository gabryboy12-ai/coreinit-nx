#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *OSDynLoad_Module;

typedef enum OSDynLoad_Error {
    OS_DYNLOAD_OK                      = 0,
    OS_DYNLOAD_OUT_OF_MEMORY           = 0xBAD10002,
    OS_DYNLOAD_INVALID_MODULE_NAME_PTR = 0xBAD1000F,
    OS_DYNLOAD_INVALID_MODULE_NAME     = 0xBAD10010,
    OS_DYNLOAD_INVALID_ACQUIRE_PTR     = 0xBAD10011,
    OS_DYNLOAD_EMPTY_MODULE_NAME       = 0xBAD10012,
    OS_DYNLOAD_INVALID_ALLOCATOR_PTR   = 0xBAD10017,
    OS_DYNLOAD_MODULE_NOT_FOUND        = 0xFFFFFFFA,
} OSDynLoad_Error;

typedef enum OSDynLoad_ExportType {
    OS_DYNLOAD_EXPORT_FUNC = 0,
    OS_DYNLOAD_EXPORT_DATA = 1,
} OSDynLoad_ExportType;

typedef OSDynLoad_Error (*OSDynLoadAllocFn)(int32_t size, int32_t align,
                                            void **outAddr);
typedef void (*OSDynLoadFreeFn)(void *addr);

OSDynLoad_Error OSDynLoad_Acquire(char const *name,
                                  OSDynLoad_Module *outModule);
OSDynLoad_Error OSDynLoad_FindExport(OSDynLoad_Module module,
                                     OSDynLoad_ExportType exportType,
                                     char const *name, void **outAddr);
void            OSDynLoad_Release(OSDynLoad_Module module);
OSDynLoad_Error OSDynLoad_SetAllocator(OSDynLoadAllocFn allocFn,
                                       OSDynLoadFreeFn freeFn);
OSDynLoad_Error OSDynLoad_GetAllocator(OSDynLoadAllocFn *outAllocFn,
                                       OSDynLoadFreeFn *outFreeFn);
int32_t         OSDynLoad_GetNumberOfRPLs(void);
int32_t         OSDynLoad_GetRPLInfo(uint32_t first, uint32_t count,
                                     void *outInfo);

#ifdef __cplusplus
}
#endif