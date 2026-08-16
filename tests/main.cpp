#include "coreinit/mutex.h"
#include "coreinit/condition.h"
#include "coreinit/thread.h"
#include "coreinit/time.h"
#include "coreinit/systeminfo.h"
#include "coreinit/cache.h"

#include <switch.h>
#include <cstdio>

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


    printf("\n%d fallimenti. Premi + per uscire.\n", g_failures);
    consoleUpdate(nullptr);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}