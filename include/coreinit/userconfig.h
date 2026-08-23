#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t UCHandle;
typedef int32_t UCError;

typedef enum UCDataType {
    UC_DATATYPE_UNDEFINED      = 0x00,
    UC_DATATYPE_UNSIGNED_BYTE  = 0x01,
    UC_DATATYPE_UNSIGNED_SHORT = 0x02,
    UC_DATATYPE_UNSIGNED_INT   = 0x03,
    UC_DATATYPE_SIGNED_INT     = 0x04,
    UC_DATATYPE_FLOAT          = 0x05,
    UC_DATATYPE_STRING         = 0x06,
    UC_DATATYPE_HEXBINARY      = 0x07,
    UC_DATATYPE_COMPLEX        = 0x08,
    UC_DATATYPE_INVALID        = 0xFF,
} UCDataType;

typedef enum UCErrors {
    UC_ERROR_OK            = 0,
    UC_ERROR_ERROR         = -1,
    UC_ERROR_INVALID_PARAM = -0x200005,
    UC_ERROR_INVALID_TYPE  = -0x200006,
    UC_ERROR_UNSUPPORTED   = -0x200007,
    UC_ERROR_KEY_NOT_FOUND = -0x200009,
    UC_ERROR_WRITE_ACCESS  = -0x200010,
} UCErrors;

#pragma pack(push, 1)
typedef struct UCSysConfig {
    char       name[64];
    uint32_t   access;
    UCDataType dataType;
    UCError    error;
    uint32_t   dataSize;
    void      *data;
} UCSysConfig;
#pragma pack(pop)

UCHandle UCOpen(void);
UCError  UCClose(UCHandle handle);
UCError  UCReadSysConfig(UCHandle handle, uint32_t count,
                         UCSysConfig *settings);
UCError  UCWriteSysConfig(UCHandle handle, uint32_t count,
                          UCSysConfig *settings);
UCError  UCDeleteSysConfig(UCHandle handle, uint32_t count,
                           UCSysConfig *settings);

#ifdef __cplusplus
}
#endif