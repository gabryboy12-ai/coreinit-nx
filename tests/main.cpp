#include "coreinit/mutex.h"
#include "coreinit/condition.h"

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