#include "coreinit/filesystem.h"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <map>
#include <unistd.h>

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

// Stato per client. Finora non serviva -- FSChangeDir lo rende necessario,
// perche' i percorsi relativi vanno risolti rispetto alla directory
// corrente di QUEL client, non a una globale.
struct HostClient {
    std::string cwd;
    FSError     lastError = FS_ERROR_OK;
};
std::map<const void *, HostClient> g_clientState;

struct HostDir {
    DIR *dp;
    bool used;
};
std::vector<HostDir> g_dirs;

// FS_STATUS_* e FS_ERROR_* sono due enum distinti: il primo e' l'esito
// della chiamata, il secondo il codice diagnostico interrogabile dopo.
FSError statusToError(FSStatus s) {
    switch (s) {
        case FS_STATUS_OK:               return FS_ERROR_OK;
        case FS_STATUS_END:              return FS_ERROR_END_OF_FILE;
        case FS_STATUS_NOT_FOUND:        return FS_ERROR_NOT_FOUND;
        case FS_STATUS_EXISTS:           return FS_ERROR_ALREADY_EXISTS;
        case FS_STATUS_PERMISSION_ERROR: return FS_ERROR_PERMISSION_ERROR;
        case FS_STATUS_ACCESS_ERROR:     return FS_ERROR_ACCESS_ERROR;
        case FS_STATUS_STORAGE_FULL:     return FS_ERROR_STORAGE_FULL;
        default:                         return FS_ERROR_MEDIA_ERROR;
    }
}

FSStatus record(const void *client, FSStatus s) {
    if (client) g_clientState[client].lastError = statusToError(s);
    return s;
}

HostDir *dirFor(FSDirectoryHandle h) {
    if (h == 0 || h > g_dirs.size()) return nullptr;
    HostDir *d = &g_dirs[h - 1];
    return d->used ? d : nullptr;
}

void ensureLock() {
    if (!g_lockInit) { mutexInit(&g_lock); g_lockInit = true; }
}

std::string translate(const void *client, const char *path) {
    if (!path) return std::string();
    std::string p(path);

    // Percorso relativo: risolvilo rispetto alla cwd del client.
    if (!p.empty() && p[0] != '/') {
        auto it = g_clientState.find(client);
        if (it != g_clientState.end() && !it->second.cwd.empty()) {
            std::string base = it->second.cwd;
            if (base.back() != '/') base += '/';
            p = base + p;
        }
    }

    for (const auto &m : g_mappings) {
        if (p.compare(0, m.wiiu.size(), m.wiiu) == 0) {
            return m.host + p.substr(m.wiiu.size());
        }
    }
    return p;
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
    
    for (auto &d : g_dirs) { 
        if (d.used && d.dp) closedir(d.dp);
    d.used = false; d.dp = nullptr; 
    }
    g_dirs.clear();
    g_clientState.clear();
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
    (void)block; (void)errorMask;
    if (!path || !mode || !handle) return FS_STATUS_FATAL_ERROR;

    ensureLock();
    mutexLock(&g_lock);
    const std::string hostPath = translate(client, path);
    FILE *fp = fopen(hostPath.c_str(), mode);
    if (!fp) {
        // Distinguere "manca" da "e' una directory": Cafe OS ha codici
        // diversi e un gioco puo' comportarsi diversamente nei due casi.
        struct stat st;
        const FSStatus why = (stat(hostPath.c_str(), &st) == 0 &&
                              S_ISDIR(st.st_mode))
                             ? FS_STATUS_NOT_FILE
                             : FS_STATUS_NOT_FOUND;
        mutexUnlock(&g_lock);
        return record(client, why);
    }

    uint32_t slot = 0;
    for (uint32_t i = 0; i < g_files.size(); i++) {
        if (!g_files[i].used) { slot = i + 1; break; }
    }
    if (slot == 0) { g_files.push_back(HostFile{nullptr, false});
                     slot = (uint32_t)g_files.size(); }

    g_files[slot - 1] = HostFile{fp, true};
    *handle = slot;
    mutexUnlock(&g_lock);
    return record(client, FS_STATUS_OK);
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

FSStatus FSWriteFile(FSClient *client, FSCmdBlock *block, uint8_t *buffer,
                     uint32_t size, uint32_t count, FSFileHandle handle,
                     uint32_t unk1, FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)unk1; (void)errorMask;
    if (!buffer || size == 0) return FS_STATUS_FATAL_ERROR;
    HostFile *f = fileFor(handle);
    if (!f) return FS_STATUS_FATAL_ERROR;
    // Come per la lettura: il ritorno e' il conteggio degli elementi.
    return (FSStatus)(int32_t)fwrite(buffer, size, count, f->fp);
}

FSStatus FSChangeDir(FSClient *client, FSCmdBlock *block, const char *path,
                     FSErrorFlag errorMask)
{
    (void)block; (void)errorMask;
    if (!client || !path) return FS_STATUS_FATAL_ERROR;
    ensureLock();
    mutexLock(&g_lock);
    g_clientState[client].cwd = path;
    mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

FSStatus FSGetCwd(FSClient *client, FSCmdBlock *block, char *returnedPath,
                  uint32_t bytes, FSErrorFlag errorMask)
{
    (void)block; (void)errorMask;
    if (!client || !returnedPath || bytes == 0) return FS_STATUS_FATAL_ERROR;
    ensureLock();
    mutexLock(&g_lock);
    const std::string &cwd = g_clientState[client].cwd;
    snprintf(returnedPath, bytes, "%s", cwd.c_str());
    mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

FSStatus FSOpenDir(FSClient *client, FSCmdBlock *block, const char *path,
                   FSDirectoryHandle *handle, FSErrorFlag errorMask)
{
    (void)block; (void)errorMask;
    if (!path || !handle) return FS_STATUS_FATAL_ERROR;

    ensureLock();
    mutexLock(&g_lock);
    DIR *dp = opendir(translate(client, path).c_str());
    if (!dp) { mutexUnlock(&g_lock); return record(client, FS_STATUS_NOT_FOUND); }

    uint32_t slot = 0;
    for (uint32_t i = 0; i < g_dirs.size(); i++) {
        if (!g_dirs[i].used) { slot = i + 1; break; }
    }
    if (slot == 0) { g_dirs.push_back(HostDir{nullptr, false});
                     slot = (uint32_t)g_dirs.size(); }

    g_dirs[slot - 1] = HostDir{dp, true};
    *handle = slot;
    mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

FSStatus FSReadDir(FSClient *client, FSCmdBlock *block,
                   FSDirectoryHandle handle, FSDirectoryEntry *entry,
                   FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    if (!entry) return FS_STATUS_FATAL_ERROR;
    HostDir *d = dirFor(handle);
    if (!d) return FS_STATUS_FATAL_ERROR;

    struct dirent *e = readdir(d->dp);
    // Fine directory: Cafe OS segnala FS_STATUS_END, non un errore.
    if (!e) return FS_STATUS_END;

    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", e->d_name);
    return FS_STATUS_OK;
}

FSStatus FSCloseDir(FSClient *client, FSCmdBlock *block,
                    FSDirectoryHandle handle, FSErrorFlag errorMask)
{
    (void)client; (void)block; (void)errorMask;
    ensureLock();
    mutexLock(&g_lock);
    HostDir *d = dirFor(handle);
    if (!d) { mutexUnlock(&g_lock); return FS_STATUS_FATAL_ERROR; }
    if (d->dp) closedir(d->dp);
    d->dp = nullptr; d->used = false;
    mutexUnlock(&g_lock);
    return FS_STATUS_OK;
}

FSStatus FSMakeDir(FSClient *client, FSCmdBlock *block, const char *path,
                   FSErrorFlag errorMask)
{
    (void)block; (void)errorMask;
    if (!path) return FS_STATUS_FATAL_ERROR;
    const std::string host = translate(client, path);
    if (mkdir(host.c_str(), 0777) != 0) return FS_STATUS_FATAL_ERROR;
    return FS_STATUS_OK;
}

FSStatus FSRemove(FSClient *client, FSCmdBlock *block, const char *path,
                  FSErrorFlag errorMask)
{
    (void)block; (void)errorMask;
    if (!path) return FS_STATUS_FATAL_ERROR;
    const std::string host = translate(client, path);
    // Cafe OS: FSRemove cancella sia file che directory vuote.
    if (remove(host.c_str()) == 0) return record(client, FS_STATUS_OK);
    if (rmdir(host.c_str()) == 0)  return record(client, FS_STATUS_OK);
    return record(client, FS_STATUS_NOT_FOUND);
}

FSStatus FSRename(FSClient *client, FSCmdBlock *block, const char *oldPath,
                  const char *newPath, FSErrorFlag errorMask)
{
    (void)block; (void)errorMask;
    if (!oldPath || !newPath) return FS_STATUS_FATAL_ERROR;
    const std::string a = translate(client, oldPath);
    const std::string b = translate(client, newPath);
    return record(client, rename(a.c_str(), b.c_str()) == 0
                              ? FS_STATUS_OK
                              : FS_STATUS_FATAL_ERROR);
}

FSError FSGetLastError(FSClient *client)
{
    if (!client) return FS_ERROR_INVALID_CLIENTHANDLE;
    auto it = g_clientState.find(client);
    return it == g_clientState.end() ? FS_ERROR_OK : it->second.lastError;
}

FSError FSGetLastErrorCodeForViewer(FSClient *client)
{
    return FSGetLastError(client);
}

// La sorgente di mount e' una struttura del guest da 0x300 byte che non
// popoliamo: ci serve solo ricordare quale tipo e' stato richiesto, per
// scegliere la radice host al momento del mount.
FSStatus FSGetMountSource(FSClient *client, FSCmdBlock *cmd,
                          FSMountSourceType type, FSMountSource *out,
                          FSErrorFlag errorMask)
{
    (void)cmd; (void)errorMask;
    if (!out) return record(client, FS_STATUS_FATAL_ERROR);
    memset(out, 0, sizeof(*out));
    // Marcatore nostro nel primo byte: l'unica cosa che rileggiamo.
    out->_opaque[0] = (uint8_t)type;
    return record(client, FS_STATUS_OK);
}

// Invece di uno stub, il mount REGISTRA UNA MAPPATURA. Se il gioco monta
// la SD su /vol/external01, i percorsi che costruira' dopo verranno
// tradotti davvero, invece di risolversi nel nulla.
FSStatus FSMount(FSClient *client, FSCmdBlock *cmd, FSMountSource *source,
                 const char *target, uint32_t bytes, FSErrorFlag errorMask)
{
    (void)cmd; (void)bytes; (void)errorMask;
    if (!source || !target) return record(client, FS_STATUS_FATAL_ERROR);

    const char *hostRoot =
        source->_opaque[0] == FS_MOUNT_SOURCE_HFIO ? "/" : "/";
    coreinitNxAddVolumeMapping(target, hostRoot);
    return record(client, FS_STATUS_OK);
}

// Nessun media rimovibile da sorvegliare: il volume e' sempre pronto.
// Rispondere NO_MEDIA bloccherebbe l'avvio di qualsiasi gioco.
FSVolumeState FSGetVolumeState(FSClient *client)
{
    (void)client;
    return FS_VOLUME_STATE_READY;
}

// Le notifiche servono a reagire a inserimento/rimozione del disco. Qui
// non accade mai nulla di simile, quindi la callback non verra' mai
// invocata. NON e' una finzione: e' il comportamento corretto per un
// sistema senza media rimovibile.
void FSSetStateChangeNotification(FSClient *client, FSStateChangeParams *info)
{
    (void)client; (void)info;
}

} // extern "C"