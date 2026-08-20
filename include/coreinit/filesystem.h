#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t FSFileHandle;
typedef uint32_t FSDirectoryHandle;
typedef uint32_t FSStatFlags;
typedef uint32_t FSMode;
typedef uint64_t FSTime;

// Da wut: FSClient 0x1700, FSCmdBlock 0xA80, FSStat 0x64
typedef struct FSClient   { uint8_t _opaque[0x1700]; } FSClient;
typedef struct FSCmdBlock { uint8_t _opaque[0x0A80]; } FSCmdBlock;

typedef struct FSStat {
    FSStatFlags flags;
    FSMode      mode;
    uint32_t    owner;
    uint32_t    group;
    uint32_t    size;
    uint32_t    allocSize;
    uint64_t    quotaSize;
    uint32_t    entryId;
    FSTime      created;
    FSTime      modified;
    uint8_t     _unknown[0x30];
} FSStat;

typedef enum FSErrorFlag {
    FS_ERROR_FLAG_NONE = 0x0,
    FS_ERROR_FLAG_ALL  = 0xFFFFFFFF,
} FSErrorFlag;

typedef enum FSStatus {
    FS_STATUS_OK               = 0,
    FS_STATUS_CANCELLED        = -1,
    FS_STATUS_END              = -2,
    FS_STATUS_MAX              = -3,
    FS_STATUS_ALREADY_OPEN     = -4,
    FS_STATUS_EXISTS           = -5,
    FS_STATUS_NOT_FOUND        = -6,
    FS_STATUS_NOT_FILE         = -7,
    FS_STATUS_NOT_DIR          = -8,
    FS_STATUS_ACCESS_ERROR     = -9,
    FS_STATUS_PERMISSION_ERROR = -10,
    FS_STATUS_FILE_TOO_BIG     = -11,
    FS_STATUS_STORAGE_FULL     = -12,
    FS_STATUS_JOURNAL_FULL     = -13,
    FS_STATUS_UNSUPPORTED_CMD  = -14,
    FS_STATUS_MEDIA_NOT_READY  = -15,
    FS_STATUS_MEDIA_ERROR      = -17,
    FS_STATUS_CORRUPTED        = -18,
    FS_STATUS_FATAL_ERROR      = -0x400,
} FSStatus;

void     FSInit(void);
void     FSShutdown(void);
FSStatus FSAddClient(FSClient *client, FSErrorFlag errorMask);
FSStatus FSDelClient(FSClient *client, FSErrorFlag errorMask);
uint32_t FSGetClientNum(void);
void     FSInitCmdBlock(FSCmdBlock *block);

FSStatus FSOpenFile(FSClient *client, FSCmdBlock *block, const char *path,
                    const char *mode, FSFileHandle *handle,
                    FSErrorFlag errorMask);
FSStatus FSCloseFile(FSClient *client, FSCmdBlock *block,
                     FSFileHandle handle, FSErrorFlag errorMask);
FSStatus FSReadFile(FSClient *client, FSCmdBlock *block, uint8_t *buffer,
                    uint32_t size, uint32_t count, FSFileHandle handle,
                    uint32_t unk1, FSErrorFlag errorMask);
FSStatus FSReadFileWithPos(FSClient *client, FSCmdBlock *block,
                           uint8_t *buffer, uint32_t size, uint32_t count,
                           uint32_t pos, FSFileHandle handle,
                           uint32_t unk1, FSErrorFlag errorMask);
FSStatus FSGetPosFile(FSClient *client, FSCmdBlock *block,
                      FSFileHandle fileHandle, uint32_t *pos,
                      FSErrorFlag errorMask);
FSStatus FSSetPosFile(FSClient *client, FSCmdBlock *block,
                      FSFileHandle handle, uint32_t pos,
                      FSErrorFlag errorMask);
FSStatus FSGetStatFile(FSClient *client, FSCmdBlock *block,
                       FSFileHandle handle, FSStat *stat,
                       FSErrorFlag errorMask);

// --- estensione coreinit-nx, non parte di Cafe OS ---
// Mappa un prefisso di percorso Wii U su uno dell'host. Dove stiano i dati
// di gioco e' una decisione del port, non di questa libreria.
//   coreinitNxAddVolumeMapping("/vol/content", "/wiiu/bo2/content");
void coreinitNxAddVolumeMapping(const char *wiiuPrefix, const char *hostPrefix);
void coreinitNxClearVolumeMappings(void);

#ifdef __cplusplus
}
#endif