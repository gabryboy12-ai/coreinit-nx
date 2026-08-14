#include "coreinit/mutex.h"

#include <switch.h>
#include <cstdio>

static int g_failures = 0;

static void check(bool condition, const char *label)
{
    printf(condition ? "[PASS] %s\n" : "[FAIL] %s\n", label);
    if (!condition) {
        g_failures++;
    }
    consoleUpdate(nullptr);
}

static void test_basic_lock()
{
    OSMutex mutex;
    OSInitMutex(&mutex);
    OSLockMutex(&mutex);
    OSUnlockMutex(&mutex);
    check(true, "lock/unlock semplice");
}

static void test_recursive_lock()
{
    // Se i mutex Cafe OS non fossero ricorsivi, qui andremmo in deadlock
    // e la funzione non tornerebbe mai. Il fatto che ritorni E' il test:
    // se la console si pianta senza stampare, la risposta e' quella.
    OSMutex mutex;
    OSInitMutex(&mutex);
    OSLockMutex(&mutex);
    OSLockMutex(&mutex);
    OSUnlockMutex(&mutex);
    OSUnlockMutex(&mutex);
    check(true, "lock ricorsivo");
}

static void test_try_lock()
{
    OSMutex mutex;
    OSInitMutex(&mutex);
    check(OSTryLockMutex(&mutex) == 1, "trylock su mutex libero");
    OSUnlockMutex(&mutex);
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

    printf("\n%d fallimenti. Premi + per uscire.\n", g_failures);
    consoleUpdate(nullptr);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
