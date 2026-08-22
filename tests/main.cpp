#include "coreinit/mutex.h"
#include "coreinit/condition.h"
#include "coreinit/thread.h"
#include "coreinit/time.h"
#include "coreinit/systeminfo.h"
#include "coreinit/cache.h"
#include "coreinit/memfrmheap.h"
#include "coreinit/memexpheap.h"
#include "coreinit/debug.h"
#include "coreinit/foreground.h"
#include "coreinit/internal.h"
#include "coreinit/filesystem.h"
#include "coreinit/atomic64.h"
#include "coreinit/alarm.h"
#include "coreinit/lockedcache.h"
#include "coreinit/semaphore.h"
#include "coreinit/spinlock.h"
#include "coreinit/memory.h"

#include <switch.h>
#include <cstdio>
#include <cstring>

static int g_failures = 0;

static void check(bool ok, const char *label)
{
    printf(ok ? "[PASS] %s\n" : "[FAIL] %s\n", label);
    if (!ok) g_failures++;
    consoleUpdate(nullptr);
}

// ---------------- mutex ----------------

static void test_basic_lock()
{
    OSMutex m;
    OSInitMutex(&m);
    OSLockMutex(&m);
    OSUnlockMutex(&m);
    check(true, "lock/unlock semplice");
}

static void test_recursive_lock()
{
    OSMutex m;
    OSInitMutex(&m);
    OSLockMutex(&m);
    OSLockMutex(&m);
    OSUnlockMutex(&m);
    OSUnlockMutex(&m);
    check(true, "lock ricorsivo");
}

static void test_try_lock()
{
    OSMutex m;
    OSInitMutex(&m);
    check(OSTryLockMutex(&m) == 1, "trylock su mutex libero");
    OSUnlockMutex(&m);
}

// ---------------- condition ----------------

static OSMutex           s_mutex;
static OSCondition       s_cond;
static volatile bool     s_ready;
static volatile int32_t  s_probe_result;

static void signaller(void *)
{
    svcSleepThread(50000000ULL);   // 50 ms
    OSLockMutex(&s_mutex);
    s_ready = true;
    OSSignalCond(&s_cond);
    OSUnlockMutex(&s_mutex);
}

// Tenta il lock da un ALTRO thread: e' l'unico modo onesto di sapere
// se il mutex e' ancora posseduto.
static void probe(void *)
{
    s_probe_result = OSTryLockMutex(&s_mutex);
    if (s_probe_result == 1) OSUnlockMutex(&s_mutex);
}

static bool spawn(Thread *t, ThreadFunc fn)
{
    s32 prio = 0x2C;
    Result rc = svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (R_FAILED(rc)) {
        printf("  svcGetThreadPriority: 0x%08X\n", (unsigned)rc);
        prio = 0x2C;
    }

    rc = threadCreate(t, fn, nullptr, nullptr, 0x10000, prio, -2);
    if (R_FAILED(rc)) {
        printf("  threadCreate: 0x%08X (prio=%ld)\n", (unsigned)rc, (long)prio);
        consoleUpdate(nullptr);
        return false;
    }

    rc = threadStart(t);
    if (R_FAILED(rc)) {
        printf("  threadStart: 0x%08X\n", (unsigned)rc);
        consoleUpdate(nullptr);
        threadClose(t);
        return false;
    }
    return true;
}

static void test_signal_wait()
{
    OSInitMutex(&s_mutex);
    OSInitCond(&s_cond);
    s_ready = false;

    Thread t;
    if (!spawn(&t, signaller)) { check(false, "creazione thread"); return; }

    OSLockMutex(&s_mutex);
    while (!s_ready) OSWaitCond(&s_cond, &s_mutex);
    OSUnlockMutex(&s_mutex);

    threadWaitForExit(&t);
    threadClose(&t);
    check(true, "signal/wait fra due thread");
}

static void test_wait_restores_depth()
{
    OSInitMutex(&s_mutex);
    OSInitCond(&s_cond);
    s_ready = false;
    s_probe_result = -1;

    Thread sig;
    if (!spawn(&sig, signaller)) { check(false, "creazione thread"); return; }

    OSLockMutex(&s_mutex);
    OSLockMutex(&s_mutex);                    // profondita' 2
    while (!s_ready) OSWaitCond(&s_cond, &s_mutex);

    OSUnlockMutex(&s_mutex);                  // 2 -> 1, deve restare nostro

    Thread p;
    if (spawn(&p, probe)) {
        threadWaitForExit(&p);
        threadClose(&p);
    }

    OSUnlockMutex(&s_mutex);                  // 1 -> 0

    threadWaitForExit(&sig);
    threadClose(&sig);

    check(s_probe_result == 0,
          "profondita' ricorsione ripristinata dopo wait");
}

static volatile bool s_thread_ran;

static int thread_entry(int argc, const char **argv)
{
    (void)argv;
    s_thread_ran = true;
    return argc + 100;
}

static void test_thread_lifecycle()
{
    static OSThread th;
    static uint8_t  guest_stack[0x8000];

    s_thread_ran = false;

    if (!OSCreateThread(&th, thread_entry, 7, nullptr,
                        guest_stack + sizeof(guest_stack), sizeof(guest_stack),
                        16, OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        check(false, "OSCreateThread");
        return;
    }

    // Cafe OS crea sospeso: dopo 50 ms non deve essere partito nulla.
    svcSleepThread(50000000ULL);
    const bool suspended = !s_thread_ran;

    OSResumeThread(&th);

    int result = -1;
    const bool joined = OSJoinThread(&th, &result) == 1;

    check(suspended, "thread creato sospeso");
    check(joined && s_thread_ran && result == 107,
          "resume, esecuzione e valore di ritorno");
}

static uint64_t coreinit_nx_expected_half_second()
{
    return (uint64_t)(OSGetSystemInfo()->busClockSpeed / 4) / 2;
}

static void test_time_monotonic()
{
    const OSTime a = OSGetSystemTime();
    svcSleepThread(10000000ULL);      // 10 ms
    const OSTime b = OSGetSystemTime();
    check(b > a, "il tempo di sistema avanza");
}

static void test_time_rate()
{
    const OSTime a = OSGetSystemTime();
    svcSleepThread(500000000ULL);     // 500 ms
    const OSTime elapsed = OSGetSystemTime() - a;

    // Attesi ~31'078'125 tick. Tolleranza generosa: lo sleep non e' preciso.
    const OSTime expected = (OSTime)coreinit_nx_expected_half_second();
    const OSTime diff = elapsed > expected ? elapsed - expected
                                           : expected - elapsed;
    printf("  attesi %lld, misurati %lld\n", (long long)expected,
                                             (long long)elapsed);
    check(diff < expected / 20, "il tempo scorre alla frequenza del Wii U");
}

static void test_epoch()
{
    // 2000-01-01 00:00:00 e' l'epoca Cafe OS: deve valere 0 tick.
    OSCalendarTime ct = {};
    ct.tm_year = 2000; ct.tm_mon = 0; ct.tm_mday = 1;
    check(OSCalendarTimeToTicks(&ct) == 0, "epoca 2000-01-01 = 0 tick");
}

static void test_calendar_roundtrip()
{
    // 29 febbraio 2024, un giovedi. Verifica anno bisestile,
    // giorno della settimana e giorno dell'anno in un colpo solo.
    OSCalendarTime in = {};
    in.tm_year = 2024; in.tm_mon = 1; in.tm_mday = 29;
    in.tm_hour = 13;   in.tm_min = 45; in.tm_sec = 30;

    OSCalendarTime out = {};
    OSTicksToCalendarTime(OSCalendarTimeToTicks(&in), &out);

    const bool ok = out.tm_year == 2024 && out.tm_mon == 1 &&
                    out.tm_mday == 29   && out.tm_hour == 13 &&
                    out.tm_min == 45    && out.tm_sec == 30 &&
                    out.tm_wday == 4    && out.tm_yday == 59;

    printf("  %04ld-%02ld-%02ld wday=%ld yday=%ld\n",
           (long)out.tm_year, (long)(out.tm_mon + 1), (long)out.tm_mday,
           (long)out.tm_wday, (long)out.tm_yday);
    check(ok, "roundtrip calendario su 2024-02-29");
}

static void test_sleep_ticks()
{
    const OSTime before = OSGetSystemTime();
    OSSleepTicks(OSGetSystemInfo()->busClockSpeed / 4 / 10);   // ~100 ms
    const OSTime elapsed = OSGetSystemTime() - before;
    const OSTime expected = OSGetSystemInfo()->busClockSpeed / 4 / 10;

    printf("  attesi ~%lld, misurati %lld\n",
           (long long)expected, (long long)elapsed);
    check(elapsed >= expected, "OSSleepTicks dorme almeno il richiesto");
}

static void test_core_id()
{
    const uint32_t id = OSGetCoreId();
    printf("  core corrente: %lu\n", (unsigned long)id);
    check(id <= 2, "OSGetCoreId resta nell'intervallo Cafe OS");
}

static void test_current_thread()
{
    OSThread *a = OSGetCurrentThread();
    OSThread *b = OSGetCurrentThread();
    check(a != nullptr && a == b, "OSGetCurrentThread e' stabile");
}

static void test_cache_ops()
{
    static uint8_t buffer[256];
    for (unsigned i = 0; i < sizeof(buffer); i++) buffer[i] = (uint8_t)i;
    DCStoreRange(buffer, sizeof(buffer));
    DCFlushRange(buffer, sizeof(buffer));
    check(buffer[42] == 42, "le operazioni di cache non corrompono i dati");
}

static uint8_t s_heapRegion[0x4000] __attribute__((aligned(64)));

static void test_frmheap_basic()
{
    MEMHeapHandle h = MEMCreateFrmHeapEx(s_heapRegion, sizeof(s_heapRegion), 0);
    if (!h) { check(false, "MEMCreateFrmHeapEx"); return; }

    void *a = MEMAllocFromFrmHeapEx(h, 256, 4);
    void *b = MEMAllocFromFrmHeapEx(h, 256, 4);

    const uintptr_t base = (uintptr_t)s_heapRegion;
    const uintptr_t end  = base + sizeof(s_heapRegion);
    const bool inside = a && b &&
        (uintptr_t)a >= base + 0x4C && (uintptr_t)a < end &&
        (uintptr_t)b >= base + 0x4C && (uintptr_t)b < end;

    check(inside, "le allocazioni cadono dentro la regione del guest");
    check(b > a, "la testa cresce verso l'alto");

    MEMDestroyFrmHeap(h);
}

static void test_frmheap_tail()
{
    MEMHeapHandle h = MEMCreateFrmHeapEx(s_heapRegion, sizeof(s_heapRegion), 0);
    void *head = MEMAllocFromFrmHeapEx(h, 256, 4);
    void *tail = MEMAllocFromFrmHeapEx(h, 256, -4);   // negativo = dalla coda

    printf("  head=%p tail=%p\n", head, tail);
    check(head && tail && tail > head,
          "allineamento negativo alloca dalla coda");

    MEMDestroyFrmHeap(h);
}

static void test_frmheap_alignment()
{
    MEMHeapHandle h = MEMCreateFrmHeapEx(s_heapRegion, sizeof(s_heapRegion), 0);
    MEMAllocFromFrmHeapEx(h, 1, 4);                  // disallinea di proposito
    void *p = MEMAllocFromFrmHeapEx(h, 64, 256);

    check(p && ((uintptr_t)p & 255) == 0, "allineamento a 256 rispettato");
    MEMDestroyFrmHeap(h);
}

static void test_frmheap_state()
{
    MEMHeapHandle h = MEMCreateFrmHeapEx(s_heapRegion, sizeof(s_heapRegion), 0);
    MEMAllocFromFrmHeapEx(h, 128, 4);

    const uint32_t before = MEMGetAllocatableSizeForFrmHeapEx(h, 4);
    MEMRecordStateForFrmHeap(h, 0x54455354);         // 'TEST'
    MEMAllocFromFrmHeapEx(h, 1024, 4);
    const uint32_t during = MEMGetAllocatableSizeForFrmHeapEx(h, 4);

    const int32_t ok = MEMFreeByStateToFrmHeap(h, 0x54455354);
    const uint32_t after = MEMGetAllocatableSizeForFrmHeapEx(h, 4);

    printf("  prima=%lu durante=%lu dopo=%lu\n",
           (unsigned long)before, (unsigned long)during, (unsigned long)after);
    check(ok == 1 && during < before && after == before,
          "record/free by state ripristina esattamente lo stato");

    MEMDestroyFrmHeap(h);
}

static void test_frmheap_exhaustion()
{
    MEMHeapHandle h = MEMCreateFrmHeapEx(s_heapRegion, sizeof(s_heapRegion), 0);
    void *big = MEMAllocFromFrmHeapEx(h, sizeof(s_heapRegion) * 2, 4);
    check(big == nullptr, "allocazione oltre la capacita' fallisce");

    MEMFreeToFrmHeap(h, MEM_FRM_HEAP_FREE_ALL);
    const uint32_t full = MEMGetAllocatableSizeForFrmHeapEx(h, 4);
    check(full > sizeof(s_heapRegion) - 0x100,
          "free ALL restituisce quasi tutta la regione");

    MEMDestroyFrmHeap(h);
}

static uint8_t s_expRegion[0x8000] __attribute__((aligned(64)));

static void test_expheap_basic()
{
    MEMHeapHandle h = MEMCreateExpHeapEx(s_expRegion, sizeof(s_expRegion), 0);
    if (!h) { check(false, "MEMCreateExpHeapEx"); return; }

    void *a = MEMAllocFromExpHeapEx(h, 256, 4);
    void *b = MEMAllocFromExpHeapEx(h, 256, 4);

    const uintptr_t base = (uintptr_t)s_expRegion;
    const uintptr_t end  = base + sizeof(s_expRegion);
    check(a && b && a != b &&
          (uintptr_t)a >= base + 0x54 && (uintptr_t)b < end,
          "due allocazioni distinte dentro la regione");

    MEMDestroyExpHeap(h);
}

static void test_expheap_free_and_reuse()
{
    MEMHeapHandle h = MEMCreateExpHeapEx(s_expRegion, sizeof(s_expRegion), 0);

    void *a = MEMAllocFromExpHeapEx(h, 512, 4);
    void *b = MEMAllocFromExpHeapEx(h, 512, 4);
    MEMFreeToExpHeap(h, a);
    void *c = MEMAllocFromExpHeapEx(h, 512, 4);

    printf("  a=%p b=%p c=%p\n", a, b, c);
    check(c == a, "il blocco liberato viene riusato");

    MEMFreeToExpHeap(h, b);
    MEMFreeToExpHeap(h, c);
    MEMDestroyExpHeap(h);
}

static void test_expheap_coalesce()
{
    MEMHeapHandle h = MEMCreateExpHeapEx(s_expRegion, sizeof(s_expRegion), 0);
    const uint32_t empty = MEMGetTotalFreeSizeForExpHeap(h);

    void *a = MEMAllocFromExpHeapEx(h, 512, 4);
    void *b = MEMAllocFromExpHeapEx(h, 512, 4);
    void *c = MEMAllocFromExpHeapEx(h, 512, 4);
    MEMFreeToExpHeap(h, a);
    MEMFreeToExpHeap(h, b);
    MEMFreeToExpHeap(h, c);

    const uint32_t after = MEMGetTotalFreeSizeForExpHeap(h);
    printf("  vuoto=%lu dopo=%lu\n",
           (unsigned long)empty, (unsigned long)after);
    check(after == empty, "liberare tutto ricompatta l'intera regione");

    MEMDestroyExpHeap(h);
}

static void test_expheap_alignment()
{
    MEMHeapHandle h = MEMCreateExpHeapEx(s_expRegion, sizeof(s_expRegion), 0);
    MEMAllocFromExpHeapEx(h, 3, 4);
    void *p = MEMAllocFromExpHeapEx(h, 64, 256);
    check(p && ((uintptr_t)p & 255) == 0, "ExpHeap rispetta l'allineamento");
    MEMDestroyExpHeap(h);
}

static void test_expheap_bottom()
{
    MEMHeapHandle h = MEMCreateExpHeapEx(s_expRegion, sizeof(s_expRegion), 0);
    void *top    = MEMAllocFromExpHeapEx(h, 256, 4);
    void *bottom = MEMAllocFromExpHeapEx(h, 256, -4);
    printf("  top=%p bottom=%p\n", top, bottom);
    check(top && bottom && bottom > top,
          "allineamento negativo alloca dal fondo");
    MEMDestroyExpHeap(h);
}

static void test_expheap_overhead()
{
    MEMHeapHandle h = MEMCreateExpHeapEx(s_expRegion, sizeof(s_expRegion), 0);
    const uint32_t before = MEMGetTotalFreeSizeForExpHeap(h);
    MEMAllocFromExpHeapEx(h, 100, 4);
    const uint32_t after = MEMGetTotalFreeSizeForExpHeap(h);

    const uint32_t consumed = before - after;
    printf("  consumati %lu per 100 richiesti\n", (unsigned long)consumed);
    check(consumed >= 100 + 0x14,
          "l'overhead di 0x14 per blocco viene addebitato");

    MEMDestroyExpHeap(h);
}

static void test_base_heap()
{
    MEMHeapHandle h = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
    if (!h) { check(false, "MEMGetBaseHeapHandle"); return; }
    void *p = MEMAllocFromExpHeapEx(h, 4096, 32);
    check(p && ((uintptr_t)p & 31) == 0, "allocazione dal base heap MEM2");
    if (p) MEMFreeToExpHeap(h, p);
}

static char     s_logBuf[512];
static uint32_t s_logLen;
static bool     s_fatalSeen;

static void captureSink(const char *text, uint32_t size)
{
    const uint32_t room = sizeof(s_logBuf) - 1 - s_logLen;
    const uint32_t n = size < room ? size : room;
    memcpy(s_logBuf + s_logLen, text, n);
    s_logLen += n;
    s_logBuf[s_logLen] = '\0';
}

// Returning from a fatal handler is a test-only affordance: on Cafe OS
// OSFatal never returns.
static void captureFatal(const char *msg)
{
    s_fatalSeen = (msg != nullptr && strcmp(msg, "boom") == 0);
}

static void test_osreport()
{
    s_logLen = 0; s_logBuf[0] = '\0';
    coreinitNxSetLogSink(captureSink);
    OSReport("value=%d name=%s", 42, "test");
    coreinitNxSetLogSink(nullptr);

    printf("  catturato: \"%s\"\n", s_logBuf);
    check(strcmp(s_logBuf, "value=42 name=test") == 0,
          "OSReport formatta e instrada al sink");
}

static void test_console_write()
{
    s_logLen = 0; s_logBuf[0] = '\0';
    coreinitNxSetLogSink(captureSink);
    OSConsoleWrite("abcdef", 3);
    coreinitNxSetLogSink(nullptr);

    check(strcmp(s_logBuf, "abc") == 0,
          "OSConsoleWrite rispetta la lunghezza richiesta");
}

static void test_os_snprintf()
{
    char buf[8];
    const int n = __os_snprintf(buf, sizeof(buf), "%s", "0123456789");
    check(strlen(buf) == 7 && n == 10,
          "__os_snprintf tronca ma riporta la lunghezza piena");
}

static void test_osfatal_handler()
{
    s_fatalSeen = false;
    coreinitNxSetFatalHandler(captureFatal);
    OSFatal("boom");
    coreinitNxSetFatalHandler(nullptr);
    check(s_fatalSeen, "OSFatal invoca il gestore installato");
}

static void test_saves_done()
{
    OSSavesDone_ReadyToRelease();
    check(true, "OSSavesDone_ReadyToRelease e' una no-op che ritorna");
}

static volatile void *s_tlsFromThread;

static int tls_worker(int argc, const char **argv)
{
    (void)argc; (void)argv;
    // Deve partire vuoto: gli slot sono per-thread, non globali.
    s_tlsFromThread = OSGetThreadSpecific(OS_THREAD_SPECIFIC_0);
    return 0;
}

static void test_thread_introspection()
{
    static OSThread th;
    static uint8_t  stack[0x4000];

    if (!OSCreateThread(&th, tls_worker, 0, nullptr,
                        stack + sizeof(stack), sizeof(stack),
                        7, OS_THREAD_ATTRIB_AFFINITY_CPU1)) {
        check(false, "OSCreateThread"); return;
    }

    check(OSGetThreadPriority(&th) == 7,
          "la priorita' torna com'era stata passata");
    check(OSGetThreadAffinity(&th) == OS_THREAD_ATTRIB_AFFINITY_CPU1,
          "l'affinita' torna come maschera originale");

    OSSetThreadName(&th, "worker");
    check(OSGetThreadName(&th) &&
          strcmp(OSGetThreadName(&th), "worker") == 0,
          "il nome del thread viene conservato");

    OSResumeThread(&th);
    int result = 0;
    OSJoinThread(&th, &result);
}

static void test_thread_specific()
{
    OSSetThreadSpecific(OS_THREAD_SPECIFIC_0, (void *)0x1234);
    check(OSGetThreadSpecific(OS_THREAD_SPECIFIC_0) == (void *)0x1234,
          "TLS: scrittura e rilettura sullo stesso thread");

    s_tlsFromThread = (void *)0xFFFF;

    static OSThread th;
    static uint8_t  stack[0x4000];
    OSCreateThread(&th, tls_worker, 0, nullptr,
                   stack + sizeof(stack), sizeof(stack),
                   16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&th);
    int result = 0;
    OSJoinThread(&th, &result);

    check(s_tlsFromThread == nullptr,
          "TLS: ogni thread parte con gli slot vuoti");

    OSSetThreadSpecific(OS_THREAD_SPECIFIC_0, nullptr);
}

static void test_filesystem()
{
    const char *hostPath = "/switch/coreinit-nx-test.bin";
    const char  payload[] = "CAFEBABE0123456789";
    const uint32_t payloadLen = sizeof(payload) - 1;

    FILE *seed = fopen(hostPath, "wb");
    if (!seed) { check(false, "impossibile creare il file di prova"); return; }
    fwrite(payload, 1, payloadLen, seed);
    fclose(seed);

    FSInit();
    coreinitNxClearVolumeMappings();
    coreinitNxAddVolumeMapping("/vol/content", "/switch");

    static FSClient   client;
    static FSCmdBlock block;
    FSAddClient(&client, FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(&block);
    check(FSGetClientNum() == 1, "FSAddClient registra la sessione");

    FSFileHandle h = 0;
    const FSStatus opened = FSOpenFile(&client, &block,
                                       "/vol/content/coreinit-nx-test.bin",
                                       "rb", &h, FS_ERROR_FLAG_ALL);
    check(opened == FS_STATUS_OK && h != 0,
          "FSOpenFile risolve il percorso /vol/ tramite la mappatura");

    if (opened == FS_STATUS_OK) {
        FSStat st;
        FSGetStatFile(&client, &block, h, &st, FS_ERROR_FLAG_ALL);
        check(st.size == payloadLen, "FSGetStatFile riporta la dimensione");

        uint8_t buf[32] = {};
        const FSStatus n = FSReadFile(&client, &block, buf, 1, 8, h, 0,
                                      FS_ERROR_FLAG_ALL);
        printf("  letti %d byte: %.8s\n", (int)n, buf);
        check((int)n == 8 && memcmp(buf, payload, 8) == 0,
              "FSReadFile restituisce il conteggio e i dati giusti");

        uint32_t pos = 0;
        FSGetPosFile(&client, &block, h, &pos, FS_ERROR_FLAG_ALL);
        check(pos == 8, "la posizione avanza dopo la lettura");

        FSSetPosFile(&client, &block, h, 4, FS_ERROR_FLAG_ALL);
        uint8_t buf2[8] = {};
        FSReadFile(&client, &block, buf2, 1, 4, h, 0, FS_ERROR_FLAG_ALL);
        check(memcmp(buf2, payload + 4, 4) == 0,
              "FSSetPosFile riposiziona correttamente");

        FSCloseFile(&client, &block, h, FS_ERROR_FLAG_ALL);
    }

    FSDelClient(&client, FS_ERROR_FLAG_ALL);
    FSShutdown();
    remove(hostPath);
}

static void test_filesystem_write_and_dirs()
{
    FSInit();
    coreinitNxClearVolumeMappings();
    coreinitNxAddVolumeMapping("/vol/save", "/switch");

    static FSClient   client;
    static FSCmdBlock block;
    FSAddClient(&client, FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(&block);

    // Scrittura
    FSFileHandle h = 0;
    const char payload[] = "written-by-coreinit-nx";
    if (FSOpenFile(&client, &block, "/vol/save/cnx-write.bin", "wb", &h,
                   FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        const FSStatus n = FSWriteFile(&client, &block, (uint8_t *)payload,
                                       1, sizeof(payload) - 1, h, 0,
                                       FS_ERROR_FLAG_ALL);
        FSCloseFile(&client, &block, h, FS_ERROR_FLAG_ALL);
        check((int)n == (int)sizeof(payload) - 1,
              "FSWriteFile scrive e riporta il conteggio");
    } else {
        check(false, "FSOpenFile in scrittura");
    }

    // Rilettura di quanto scritto
    uint8_t back[32] = {};
    if (FSOpenFile(&client, &block, "/vol/save/cnx-write.bin", "rb", &h,
                   FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        FSReadFile(&client, &block, back, 1, sizeof(payload) - 1, h, 0,
                   FS_ERROR_FLAG_ALL);
        FSCloseFile(&client, &block, h, FS_ERROR_FLAG_ALL);
    }
    check(memcmp(back, payload, sizeof(payload) - 1) == 0,
          "quanto scritto si rilegge identico");

    // Percorso relativo tramite FSChangeDir
    FSChangeDir(&client, &block, "/vol/save", FS_ERROR_FLAG_ALL);
    char cwd[64] = {};
    FSGetCwd(&client, &block, cwd, sizeof(cwd), FS_ERROR_FLAG_ALL);
    check(strcmp(cwd, "/vol/save") == 0, "FSGetCwd riporta la directory impostata");

    FSFileHandle rel = 0;
    const FSStatus relOpen = FSOpenFile(&client, &block, "cnx-write.bin",
                                        "rb", &rel, FS_ERROR_FLAG_ALL);
    check(relOpen == FS_STATUS_OK,
          "un percorso relativo si risolve rispetto alla cwd");
    if (relOpen == FS_STATUS_OK) FSCloseFile(&client, &block, rel, FS_ERROR_FLAG_ALL);

    // Directory
    FSDirectoryHandle dh = 0;
    const FSStatus dirOpen = FSOpenDir(&client, &block, "/vol/save", &dh,
                                       FS_ERROR_FLAG_ALL);
    bool foundOurFile = false;
    if (dirOpen == FS_STATUS_OK) {
        FSDirectoryEntry e;
        while (FSReadDir(&client, &block, dh, &e, FS_ERROR_FLAG_ALL)
               == FS_STATUS_OK) {
            if (strcmp(e.name, "cnx-write.bin") == 0) foundOurFile = true;
        }
        FSCloseDir(&client, &block, dh, FS_ERROR_FLAG_ALL);
    }
    check(dirOpen == FS_STATUS_OK && foundOurFile,
          "FSOpenDir/FSReadDir elencano il file appena creato");

    // Rename e remove
    FSRename(&client, &block, "/vol/save/cnx-write.bin",
             "/vol/save/cnx-renamed.bin", FS_ERROR_FLAG_ALL);
    FSFileHandle r = 0;
    const bool renamed = FSOpenFile(&client, &block, "/vol/save/cnx-renamed.bin",
                                    "rb", &r, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
    if (renamed) FSCloseFile(&client, &block, r, FS_ERROR_FLAG_ALL);
    check(renamed, "FSRename sposta il file");

    check(FSRemove(&client, &block, "/vol/save/cnx-renamed.bin",
                   FS_ERROR_FLAG_ALL) == FS_STATUS_OK,
          "FSRemove cancella il file");

        FSFileHandle miss = 0;
    FSOpenFile(&client, &block, "/vol/save/non-esiste-affatto.bin", "rb",
               &miss, FS_ERROR_FLAG_ALL);
    check(FSGetLastError(&client) == FS_ERROR_NOT_FOUND,
          "FSGetLastError riporta l'ultimo errore del client");

    FSDelClient(&client, FS_ERROR_FLAG_ALL);
    FSShutdown();
}

static volatile uint64_t s_atomicCounter;

static int atomic_worker(int argc, const char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < 10000; i++) OSAddAtomic64((volatile int64_t *)&s_atomicCounter, 1);
    return 0;
}

static void test_atomic64()
{
    uint64_t v = 0;
    OSSetAtomic64(&v, 0x1122334455667788ull);
    check(OSGetAtomic64(&v) == 0x1122334455667788ull,
          "OSSetAtomic64/OSGetAtomic64 a 64 bit pieni");

    check(OSCompareAndSwapAtomic64(&v, 0x1122334455667788ull, 42) == 1 && v == 42,
          "compare-and-swap riuscito");
    check(OSCompareAndSwapAtomic64(&v, 999, 7) == 0 && v == 42,
          "compare-and-swap fallito non modifica");

    v = 0;
    check(OSTestAndSetAtomic64(&v, 40) == 0 && v == (1ull << 40),
          "test-and-set riporta il bit precedente e lo imposta");
    check(OSTestAndClearAtomic64(&v, 40) == 1 && v == 0,
          "test-and-clear riporta il bit precedente e lo azzera");

    // Il test vero: due thread che incrementano lo stesso contatore.
    // Senza atomicita' reale il totale sarebbe minore di 20000.
    s_atomicCounter = 0;
    static OSThread a, b;
    static uint8_t sa[0x4000], sb[0x4000];

    OSCreateThread(&a, atomic_worker, 0, nullptr, sa + sizeof(sa), sizeof(sa),
                   16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSCreateThread(&b, atomic_worker, 0, nullptr, sb + sizeof(sb), sizeof(sb),
                   16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&a);
    OSResumeThread(&b);
    int r = 0;
    OSJoinThread(&a, &r);
    OSJoinThread(&b, &r);

    printf("  contatore finale: %llu\n", (unsigned long long)s_atomicCounter);
    check(s_atomicCounter == 20000,
          "20000 incrementi da due thread senza perdite");
}

static void test_filesystem_mount()
{
    FSInit();
    coreinitNxClearVolumeMappings();

    static FSClient   client;
    static FSCmdBlock block;
    FSAddClient(&client, FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(&block);

    check(FSGetVolumeState(&client) == FS_VOLUME_STATE_READY,
          "il volume si dichiara pronto");

    FSMountSource src;
    check(FSGetMountSource(&client, &block, FS_MOUNT_SOURCE_SD, &src,
                           FS_ERROR_FLAG_ALL) == FS_STATUS_OK,
          "FSGetMountSource riporta una sorgente SD");

    check(FSMount(&client, &block, &src, "/vol/external01", 0,
                  FS_ERROR_FLAG_ALL) == FS_STATUS_OK,
          "FSMount registra il punto di montaggio");

    // La prova che il mount fa qualcosa: un percorso sotto il punto
    // montato deve ora risolversi sul filesystem host.
        // Prova concreta che il mount traduce: creiamo un file sull'host e lo
    // apriamo attraverso il percorso montato.
    FILE *seed = fopen("/switch/cnx-mount.bin", "wb");
    if (seed) { fputs("mounted", seed); fclose(seed); }

    FSFileHandle h = 0;
    const FSStatus opened = FSOpenFile(&client, &block,
                                       "/vol/external01/switch/cnx-mount.bin",
                                       "rb", &h, FS_ERROR_FLAG_ALL);
    check(opened == FS_STATUS_OK,
          "un file sotto il mount si apre davvero");
    if (opened == FS_STATUS_OK) FSCloseFile(&client, &block, h, FS_ERROR_FLAG_ALL);

    // E la directory deve dare NOT_FILE, non NOT_FOUND.
    FSFileHandle d = 0;
    check(FSOpenFile(&client, &block, "/vol/external01/switch", "rb", &d,
                     FS_ERROR_FLAG_ALL) == FS_STATUS_NOT_FILE,
          "aprire una directory come file da NOT_FILE");

    remove("/switch/cnx-mount.bin");
}

static volatile int  s_alarmFired;
static volatile int  s_periodicCount;

static void alarm_cb(OSAlarm *a, OSContext *ctx)
{
    (void)a; (void)ctx;
    s_alarmFired++;
}

static void periodic_cb(OSAlarm *a, OSContext *ctx)
{
    (void)a; (void)ctx;
    s_periodicCount++;
}

static void test_alarm_oneshot()
{
    static OSAlarm a;
    s_alarmFired = 0;

    OSCreateAlarm(&a);
    OSSetAlarmUserData(&a, (void *)0xABCD);
    check(OSGetAlarmUserData(&a) == (void *)0xABCD,
          "user data dell'allarme conservato");

    const OSTime hz = OSGetSystemInfo()->busClockSpeed / 4;
    OSSetAlarm(&a, hz / 10, alarm_cb);              // fra ~100 ms

    svcSleepThread(50000000ULL);
    check(s_alarmFired == 0, "l'allarme non scatta prima del tempo");

    svcSleepThread(150000000ULL);
    check(s_alarmFired == 1, "l'allarme scatta una volta sola");
}

static void test_alarm_cancel()
{
    static OSAlarm a;
    s_alarmFired = 0;

    const OSTime hz = OSGetSystemInfo()->busClockSpeed / 4;
    OSCreateAlarm(&a);
    OSSetAlarm(&a, hz / 5, alarm_cb);               // fra ~200 ms
    check(OSCancelAlarm(&a) == 1, "OSCancelAlarm riporta che era armato");

    svcSleepThread(300000000ULL);
    check(s_alarmFired == 0, "un allarme cancellato non scatta");
}

static void test_alarm_periodic()
{
    static OSAlarm a;
    s_periodicCount = 0;

    const OSTime hz = OSGetSystemInfo()->busClockSpeed / 4;
    OSCreateAlarm(&a);
    OSSetAlarmTag(&a, 0x1234);
    OSSetPeriodicAlarm(&a, hz / 20, hz / 20, periodic_cb);

    svcSleepThread(280000000ULL);      // ~5 intervalli da 50 ms
    OSCancelAlarms(0x1234);
    const int seen = s_periodicCount;

    svcSleepThread(150000000ULL);
    printf("  scatti periodici: %d, dopo il cancel: %d\n",
           seen, s_periodicCount);
    check(seen >= 3 && s_periodicCount == seen,
          "l'allarme periodico si ripete e OSCancelAlarms lo ferma");
}

static void test_alarm_time_convention()
{
    static OSAlarm a;
    s_alarmFired = 0;

    const OSTime hz = OSGetSystemInfo()->busClockSpeed / 4;

    // hz/10 = ~100 ms come DELTA. Come istante assoluto sarebbe 0.1 secondi
    // dopo l'epoca del 2000, cioe' gia' passato da vent'anni: interpretato
    // cosi', l'allarme scatterebbe SUBITO.
    OSCreateAlarm(&a);
    OSSetAlarm(&a, hz / 10, alarm_cb);

    svcSleepThread(30000000ULL);       // 30 ms
    const int early = s_alarmFired;

    svcSleepThread(200000000ULL);      // altri 200 ms
    const int late = s_alarmFired;

    printf("  dopo 30ms: %d, dopo 230ms: %d\n", early, late);

    // early=1 -> il tempo e' ASSOLUTO (valore gia' scaduto, scatto immediato)
    // early=0, late=1 -> il tempo e' RELATIVO, come dice la doc di wut
    check(early == 0 && late == 1,
          "OSSetAlarm interpreta il tempo come durata relativa");

    OSCancelAlarm(&a);
}

static void test_alarm_shutdown()
{
    // Deve poter essere chiamata e poi ricominciare: un port che riavvia
    // il sottosistema non deve trovarsi lo scheduler morto.
    coreinitNxAlarmShutdown();

    static OSAlarm a;
    s_alarmFired = 0;
    const OSTime hz = OSGetSystemInfo()->busClockSpeed / 4;
    OSCreateAlarm(&a);
    OSSetAlarm(&a, hz / 20, alarm_cb);
    svcSleepThread(150000000ULL);

    check(s_alarmFired == 1,
          "gli allarmi ripartono dopo uno shutdown");
}

static void test_locked_cache()
{
    check(LCHardwareIsAvailable() == 1, "la locked cache si dichiara disponibile");
    check(LCGetMaxSize() == 16 * 1024, "LCGetMaxSize riporta i 16 KB dell'Espresso");

    const uint32_t before = LCGetAllocatableSize();
    void *lc = LCAlloc(1024);
    check(lc != nullptr, "LCAlloc restituisce memoria");
    check(((uintptr_t)lc & 63) == 0, "il blocco e' allineato alla linea di cache");
    check(LCGetAllocatableSize() == before - 1024,
          "lo spazio allocabile cala della quantita' richiesta");

    // Il percorso reale: DMA dalla RAM alla cache, modifica, DMA indietro.
    static uint8_t mainRam[1024];
    for (int i = 0; i < 1024; i++) mainRam[i] = (uint8_t)(i & 0xFF);

    LCEnableDMA();
    check(LCIsDMAEnabled() == 1, "il DMA risulta abilitato");

    LCLoadDMABlocks(lc, mainRam, 1024);
    LCWaitDMAQueue(LCGetDMAQueueLength());
    check(memcmp(lc, mainRam, 1024) == 0,
          "LCLoadDMABlocks copia dalla RAM alla locked cache");

    ((uint8_t *)lc)[0] = 0xAA;
    LCStoreDMABlocks(mainRam, lc, 1024);
    LCWaitDMAQueue(LCGetDMAQueueLength());
    check(mainRam[0] == 0xAA,
          "LCStoreDMABlocks riporta le modifiche in RAM");

    // Il budget e' quello vero: 16 KB, non memoria infinita.
    check(LCAlloc(64 * 1024) == nullptr,
          "un'allocazione oltre i 16 KB fallisce");

    LCDealloc(lc);
    check(LCGetAllocatableSize() == before,
          "LCDealloc restituisce lo spazio");
}

static OSSemaphore s_sem;
static volatile int s_semWorkDone;

static int sem_worker(int argc, const char **argv)
{
    (void)argc; (void)argv;
    OSWaitSemaphore(&s_sem);      // blocca finche' il main non segnala
    s_semWorkDone = 1;
    return 0;
}

static void test_semaphore()
{
    OSInitSemaphore(&s_sem, 0);
    check(OSGetSemaphoreCount(&s_sem) == 0, "il semaforo parte a zero");

    check(OSTryWaitSemaphore(&s_sem) <= 0,
          "trywait fallisce quando il conteggio e' zero");

    s_semWorkDone = 0;
    static OSThread t;
    static uint8_t stack[0x4000];
    OSCreateThread(&t, sem_worker, 0, nullptr, stack + sizeof(stack),
                   sizeof(stack), 16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&t);

    svcSleepThread(80000000ULL);
    check(s_semWorkDone == 0, "il worker resta bloccato sul semaforo");

    check(OSSignalSemaphore(&s_sem) == 0,
          "signal riporta il conteggio precedente");

    int r = 0;
    OSJoinThread(&t, &r);
    check(s_semWorkDone == 1, "il worker riparte dopo il signal");
    check(OSGetSemaphoreCount(&s_sem) == 0,
          "il conteggio torna a zero dopo la wait");
}

static OSSpinLock s_spin;
static volatile int s_spinCounter;

static int spin_worker(int argc, const char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < 5000; i++) {
        OSUninterruptibleSpinLock_Acquire(&s_spin);
        s_spinCounter++;
        OSUninterruptibleSpinLock_Release(&s_spin);
    }
    return 0;
}

static void test_spinlock()
{
    OSInitSpinLock(&s_spin);
    check(OSTryAcquireSpinLock(&s_spin) == 1, "trylock su spinlock libero");
    OSReleaseSpinLock(&s_spin);

    s_spinCounter = 0;
    static OSThread a, b;
    static uint8_t sa[0x4000], sb[0x4000];
    OSCreateThread(&a, spin_worker, 0, nullptr, sa + sizeof(sa), sizeof(sa),
                   16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSCreateThread(&b, spin_worker, 0, nullptr, sb + sizeof(sb), sizeof(sb),
                   16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&a);
    OSResumeThread(&b);
    int r = 0;
    OSJoinThread(&a, &r);
    OSJoinThread(&b, &r);

    printf("  contatore protetto: %d\n", s_spinCounter);
    check(s_spinCounter == 10000,
          "lo spinlock protegge davvero la sezione critica");
}

static volatile int s_cancelLoops;

static int cancel_worker(int argc, const char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < 1000; i++) {
        s_cancelLoops++;
        OSTestThreadCancel();          // punto di cancellazione
        svcSleepThread(1000000ULL);    // 1 ms
    }
    return 0;
}

static void test_thread_cancel()
{
    static OSThread t;
    static uint8_t stack[0x4000];
    s_cancelLoops = 0;

    OSCreateThread(&t, cancel_worker, 0, nullptr, stack + sizeof(stack),
                   sizeof(stack), 16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&t);

    svcSleepThread(50000000ULL);
    const int before = s_cancelLoops;
    OSCancelThread(&t);

    int r = 0;
    OSJoinThread(&t, &r);
    printf("  giri prima del cancel: %d, totali: %d\n", before, s_cancelLoops);

    check(before > 0 && s_cancelLoops < 1000,
          "OSCancelThread ferma il thread al punto di cancellazione");
}

static void test_cache_and_block_ops()
{
    static uint8_t buf[128] __attribute__((aligned(32)));
    memset(buf, 0xFF, sizeof(buf));

    // wut: la dimensione viene arrotondata al successivo 0x20.
    DCZeroRange(buf, 1);
    bool firstLineZero = true;
    for (int i = 0; i < 32; i++) if (buf[i] != 0) firstLineZero = false;
    check(firstLineZero && buf[32] == 0xFF,
          "DCZeroRange arrotonda alla linea di cache da 32 byte");

    uint8_t src[64], dst[64];
    for (int i = 0; i < 64; i++) src[i] = (uint8_t)i;
    check(OSBlockMove(dst, src, 64, 1) == dst && memcmp(dst, src, 64) == 0,
          "OSBlockMove copia e restituisce la destinazione");

    OSBlockSet(dst, 0x5A, 64);
    bool allSet = true;
    for (int i = 0; i < 64; i++) if (dst[i] != 0x5A) allSet = false;
    check(allSet, "OSBlockSet riempie con il valore richiesto");

    DCInvalidateRange(dst, 64);
    ICInvalidateRange(dst, 64);
    DCTouchRange(dst, 64);
    check(dst[0] == 0x5A, "le operazioni di cache non corrompono i dati");
}

int main(int argc, char **argv)
{
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("coreinit-nx :: test suite\n\n");

    test_basic_lock();
    test_recursive_lock();
    test_try_lock();
    test_signal_wait();
    test_wait_restores_depth();
    test_thread_lifecycle();
    test_time_monotonic();
    test_time_rate();
    test_epoch();
    test_calendar_roundtrip();
    test_sleep_ticks();
    test_core_id();
    test_current_thread();
    test_cache_ops();
    test_frmheap_basic();
    test_frmheap_tail();
    test_frmheap_alignment();
    test_frmheap_state();
    test_frmheap_exhaustion();
    test_expheap_basic();
    test_expheap_free_and_reuse();
    test_expheap_coalesce();
    test_expheap_alignment();
    test_expheap_bottom();
    test_expheap_overhead();
    test_base_heap();
    test_osreport();
    test_console_write();
    test_os_snprintf();
    test_osfatal_handler();
    test_saves_done();
    test_thread_introspection();
    test_thread_specific();
    test_filesystem();
    test_filesystem_write_and_dirs();
    test_atomic64();
    test_filesystem_mount();
    test_alarm_oneshot();
    test_alarm_cancel();
    test_alarm_periodic();
    test_alarm_time_convention();
    test_alarm_shutdown();
    test_locked_cache();
    test_semaphore();
    test_spinlock();
    test_thread_cancel();
    test_cache_and_block_ops();

    printf("\n%d fallimenti. Premi + per uscire.\n", g_failures);
    consoleUpdate(nullptr);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    coreinitNxAlarmShutdown();
    consoleExit(nullptr);
    return 0;
}