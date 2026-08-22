#include "coreinit/debug.h"
#include "coreinit/foreground.h"
#include "coreinit/internal.h"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <errno.h>

namespace {

void defaultLogSink(const char *text, uint32_t size)
{
    fwrite(text, 1, size, stdout);
}

void defaultFatalHandler(const char *msg)
{
    printf("\n*** OSFatal: %s\n", msg ? msg : "(null)");
    fflush(stdout);
    // Cafe OS shows an error screen the user must power off from. Blocking
    // is the closest honest equivalent; a port should install its own.
    while (true) svcSleepThread(1000000000ULL);
}

CoreinitNxLogSink      g_sink  = defaultLogSink;
CoreinitNxFatalHandler g_fatal = defaultFatalHandler;

void emit(const char *fmt, va_list args)
{
    char buf[1024];
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n <= 0) return;
    const uint32_t len = (uint32_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
    g_sink(buf, len);
}

} // namespace

extern "C" {

void coreinitNxSetLogSink(CoreinitNxLogSink sink)
{
    g_sink = sink ? sink : defaultLogSink;
}

void coreinitNxSetFatalHandler(CoreinitNxFatalHandler handler)
{
    g_fatal = handler ? handler : defaultFatalHandler;
}

void OSConsoleWrite(const char *msg, uint32_t size)
{
    if (msg && size) g_sink(msg, size);
}

void OSVReport(const char *fmt, va_list args)
{
    if (fmt) emit(fmt, args);
}

void OSReport(const char *fmt, ...)
{
    if (!fmt) return;
    va_list args; va_start(args, fmt); emit(fmt, args); va_end(args);
}

// Cafe OS filters these by log level. We do not model levels yet: all
// four go to the same sink.
void OSReportVerbose(const char *fmt, ...)
{
    if (!fmt) return;
    va_list args; va_start(args, fmt); emit(fmt, args); va_end(args);
}

void OSReportInfo(const char *fmt, ...)
{
    if (!fmt) return;
    va_list args; va_start(args, fmt); emit(fmt, args); va_end(args);
}

void OSReportWarn(const char *fmt, ...)
{
    if (!fmt) return;
    va_list args; va_start(args, fmt); emit(fmt, args); va_end(args);
}

void OSFatal(const char *msg)
{
    g_fatal(msg);
}

int __os_snprintf(char *buf, size_t n, const char *format, ...)
{
    if (!buf || !n || !format) return 0;
    va_list args; va_start(args, format);
    const int written = vsnprintf(buf, n, format, args);
    va_end(args);
    return written;
}

// On Cafe OS this tells the system that save data is written and the
// foreground can be released. Nothing here owns the foreground, so it is a
// deliberate no-op -- present because every title calls it.
void OSSavesDone_ReadyToRelease(void)
{
}

// Nessun debugger Cafe OS qui. Rispondere falso e' l'unica risposta
// onesta, e i giochi la usano solo per abilitare logging extra.
int32_t OSIsDebuggerPresent(void)     { return 0; }
int32_t OSIsDebuggerInitialized(void) { return 0; }

// OSPanic e' OSFatal con file e riga: stesso percorso.
void OSPanic(const char *file, uint32_t line, const char *fmt, ...)
{
    char buf[512];
    va_list args; va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", args);
    va_end(args);

    char full[640];
    snprintf(full, sizeof(full), "%s:%lu: %s",
             file ? file : "?", (unsigned long)line, buf);
    OSFatal(full);
}

// newlib provides __errno(); the GHS name for the same thing.
int *__gh_errno_ptr(void)
{
    return __errno();
}

// GHS C++ exception runtime. We do not know what these hooks do, and
// inventing behaviour would be worse than admitting absence. Null,
// documented, and left for anyone who can find out.
void *__cpp_exception_init_ptr    = nullptr;
void *__cpp_exception_cleanup_ptr = nullptr;
void *__atexit_cleanup            = nullptr;


} // extern "C"