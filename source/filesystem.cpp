#include "coreinit/filesystem.h"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Handles are uint32_t, not pointers, so we mint them ourselves and keep
// the real FILE* host-side. Cafe OS starts at 0 but we start at 1 so that
// a zeroed struct never looks like a valid handle.
struct HostFile {
    FILE    *fp;
    bool     used;
};

std::vector<HostFile> g_files;
uint32_t              g_clients = 0;
bool                  g_init = false;
::Mutex               g_lock;
bool                  g_lockInit = false;

struct Mapping { std::string wiiu, host; };
std::vector<Mapping> g_mappings;

void ensureLock() {
    if (!g_lockInit) { mutexInit(&g_lock); g_lockInit = true; }
}

std::string translate(const char *path) {
    if (!path) return std::string();
    std::string p(path);
    for (const auto &m : g_mappings) {
        if (p.compare(0, m.wiiu.size(), m.wiiu) == 0) {
            return m.host + p.substr(m.wiiu.size());
        }
    }
    return p;   // nessuna mappatura: percorso invariato
}

HostFile *fileFor(FSFileHandle h) {
    if (h == 0 || h > g_files.size()) return nullptr;
    HostFile *f = &g_files[h - 1];
    return f->used ? f : nullptr;
}

} // namespace

extern "C" {

void coreinitNxAddVolumeMapping(const char *wiiuPrefix, const char *hostPrefix)
{
    if (!wiiuPrefix || !hostPrefix) return;
    ensureLock();
    mutexLock(&g_lock);
    g_mappings.push_back(Mapping{wiiuPrefix, hostPrefix});
    mutexUnlock(&g_lock);
}

void coreinitNxClearVolumeMappings(void)
{
    ensureLock();
    mutexLock(&g_lock);
    g_mappings.clear();
    mutexUnlock(&g_lock);
}

void FSInit(void)   { ensureLock(); g_init = true; }

void FSShutdown(void)
{
    ensureLock();
    mutexLock(&g_lock);
    for (auto &f : g_files) {
        if (f.used && f.fp) fclose(f.fp);
        f.used = false; f.fp = nullptr;
    }
    g_files.clear();
    g_clients = 0;
    g_init = false;
    mutexUnlock(&g_lock);
}

// FSClient e FSCmdBlock sono strutture del guest da 0x1700 e 0xA80 byte.
// Non le tocchiamo: la sessione non ha stato che ci serva davvero, quindi
// ci limitiamo a contare i client per FSGetClientNum.
FSStatus FSAddClient(FSClient *client, FSErrorFlag errorMask)
{
    (void)errorMask;
    if (!client) return FS_STATUS_FATAL_ERROR;
    ensureLock();
    mutexLock(&g_lock); g_clients++; mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

FSStatus FSDelClient(FSClient *client, FSErrorFlag errorMask)
{
    (void)errorMask;
    if (!client) return FS_STATUS_FATAL_ERROR;
    ensureLock();
    mutexLock(&g_lock); if (g_clients) g_clients--; mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

uint32_t FSGetClientNum(void) { return g_clients; }

void FSInitCmdBlock(FSCmdBlock *block) { (void)block; }

FSStatus FSOpenFile(FSClient *client, FSCmdBlock *block, const char *path,
                    const char *mode, FSFileHandle *handle,
                    FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    if (!path || !mode || !handle) return FS_STATUS_FATAL_ERROR;

    ensureLock();
    mutexLock(&g_lock);
    const std::string hostPath = translate(path);
    FILE *fp = fopen(hostPath.c_str(), mode);
    if (!fp) { mutexUnlock(&g_lock); return FS_STATUS_NOT_FOUND; }

    uint32_t slot = 0;
    for (uint32_t i = 0; i < g_files.size(); i++) {
        if (!g_files[i].used) { slot = i + 1; break; }
    }
    if (slot == 0) { g_files.push_back(HostFile{nullptr, false});
                     slot = (uint32_t)g_files.size(); }

    g_files[slot - 1] = HostFile{fp, true};
    *handle = slot;
    mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

FSStatus FSCloseFile(FSClient *client, FSCmdBlock *block,
                     FSFileHandle handle, FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    ensureLock();
    mutexLock(&g_lock);
    HostFile *f = fileFor(handle);
    if (!f) { mutexUnlock(&g_lock); return FS_STATUS_FATAL_ERROR; }
    if (f->fp) fclose(f->fp);
    f->fp = nullptr; f->used = false;
    mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

// ASSUNZIONE da confermare su decaf-emu: le funzioni di lettura Cafe OS
// restituiscono il NUMERO DI ELEMENTI LETTI, non FS_STATUS_OK. Positivo =
// conteggio, negativo = errore.
FSStatus FSReadFile(FSClient *client, FSCmdBlock *block, uint8_t *buffer,
                    uint32_t size, uint32_t count, FSFileHandle handle,
                    uint32_t unk1, FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)unk1; (void)errorMask;
    if (!buffer || size == 0) return FS_STATUS_FATAL_ERROR;

    HostFile *f = fileFor(handle);
    if (!f) return FS_STATUS_FATAL_ERROR;

    const size_t read = fread(buffer, size, count, f->fp);
    return (FSStatus)(int32_t)read;
}

FSStatus FSReadFileWithPos(FSClient *client, FSCmdBlock *block,
                           uint8_t *buffer, uint32_t size, uint32_t count,
                           uint32_t pos, FSFileHandle handle,
                           uint32_t unk1, FSErrorFlag errorMask)
{
    HostFile *f = fileFor(handle);
    if (!f) return FS_STATUS_FATAL_ERROR;
    if (fseek(f->fp, (long)pos, SEEK_SET) != 0) return FS_STATUS_FATAL_ERROR;
    return FSReadFile(client, block, buffer, size, count, handle,
                      unk1, errorMask);
}

FSStatus FSGetPosFile(FSClient *client, FSCmdBlock *block,
                      FSFileHandle fileHandle, uint32_t *pos,
                      FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    if (!pos) return FS_STATUS_FATAL_ERROR;
    HostFile *f = fileFor(fileHandle);
    if (!f) return FS_STATUS_FATAL_ERROR;
    const long p = ftell(f->fp);
    if (p < 0) return FS_STATUS_FATAL_ERROR;
    *pos = (uint32_t)p;
    return FS_STATUS_OK;
}

FSStatus FSSetPosFile(FSClient *client, FSCmdBlock *block,
                      FSFileHandle handle, uint32_t pos,
                      FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    HostFile *f = fileFor(handle);
    if (!f) return FS_STATUS_FATAL_ERROR;
    return fseek(f->fp, (long)pos, SEEK_SET) == 0 ? FS_STATUS_OK
                                                  : FS_STATUS_FATAL_ERROR;
}

FSStatus FSGetStatFile(FSClient *client, FSCmdBlock *block,
                       FSFileHandle handle, FSStat *stat,
                       FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    if (!stat) return FS_STATUS_FATAL_ERROR;
    HostFile *f = fileFor(handle);
    if (!f) return FS_STATUS_FATAL_ERROR;

    const long here = ftell(f->fp);
    if (here < 0 || fseek(f->fp, 0, SEEK_END) != 0) return FS_STATUS_FATAL_ERROR;
    const long end = ftell(f->fp);
    fseek(f->fp, here, SEEK_SET);
    if (end < 0) return FS_STATUS_FATAL_ERROR;

    memset(stat, 0, sizeof(*stat));
    stat->size      = (uint32_t)end;
    stat->allocSize = (uint32_t)end;
    return FS_STATUS_OK;
}

} // extern "C"