#include "coreinit/thread.h"
#include "coreinit/foreground.h"
#include "coreinit/systeminfo.h"
#include "coreinit/atomic64.h"
#include "coreinit/exception.h"

#include <switch.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {

bool g_homeButtonEnabled = true;

constexpr int kExceptionTypes = 15;
OSExceptionCallbackFn g_exceptionHandlers[kExceptionTypes] = {};

} // namespace

extern "C" {

// Yield to another thread of equal priority. Horizon's svcSleepThread(0)
// does exactly this.
void OSYieldThread(void)
{
    svcSleepThread(0);
}

// PowerPC 'sync': a full memory barrier. Same as OSMemoryBarrier, but games
// call the raw PPC name too.
void PPCSync(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// Like OSCompareAndSwapAtomic64 but also reports the value found.
int32_t OSCompareAndSwapAtomicEx64(volatile uint64_t *ptr, uint64_t compare,
                                   uint64_t value, uint64_t *outOld)
{
    uint64_t expected = compare;
    const bool swapped = __atomic_compare_exchange_n(
        ptr, &expected, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (outOld) *outOld = expected;   // su fallimento contiene il valore letto
    return swapped ? 1 : 0;
}

// On Cafe OS these hand the screen back to the system menu. Horizon manages
// application lifecycle itself through appletMainLoop, so there is nothing
// to release. Documented no-ops rather than fake behaviour.
void OSReleaseForeground(void) {}
void OSEnableForegroundExit(void) {}

// The HOME button on Horizon is handled by the system, and homebrew cannot
// suppress it. We remember the flag so the getter stays consistent, but it
// has no effect -- a game that disables HOME during a cutscene will find it
// still works.
int32_t OSEnableHomeButtonMenu(int32_t enable)
{
    const int32_t previous = g_homeButtonEnabled ? 1 : 0;
    g_homeButtonEnabled = (enable != 0);
    return previous;
}

int32_t OSIsHomeButtonMenuEnabled(void)
{
    return g_homeButtonEnabled ? 1 : 0;
}

// Registered and never invoked. Cafe OS routes PowerPC exceptions here --
// alignment faults, DSI, program traps. Under Horizon those become Horizon
// exceptions with a different mechanism, and recompiled code will not raise
// PowerPC exceptions at all. Stored so the setter chains correctly.
OSExceptionCallbackFn OSSetExceptionCallback(OSExceptionType exceptionType,
                                             OSExceptionCallbackFn callback)
{
    const int idx = (int)exceptionType;
    if (idx < 0 || idx >= kExceptionTypes) return nullptr;
    OSExceptionCallbackFn previous = g_exceptionHandlers[idx];
    g_exceptionHandlers[idx] = callback;
    return previous;
}

// Cafe OS exposes the process environment. Horizon has none, so we serve
// from the host environment where a name matches and report absence
// otherwise. A port can set variables with setenv() before starting.
int32_t ENVGetEnvironmentVariable(const char *name, char *buffer,
                                  uint32_t length)
{
    if (!name || !buffer || length == 0) return 0;
    const char *value = getenv(name);
    if (!value) { buffer[0] = '\0'; return 0; }
    snprintf(buffer, length, "%s", value);
    return 1;
}

} // extern "C"