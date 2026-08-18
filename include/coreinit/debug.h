#pragma once
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void OSConsoleWrite(const char *msg, uint32_t size);
void OSReport(const char *fmt, ...);
void OSReportVerbose(const char *fmt, ...);
void OSReportInfo(const char *fmt, ...);
void OSReportWarn(const char *fmt, ...);
void OSVReport(const char *fmt, va_list args);
void OSFatal(const char *msg);

// --- coreinit-nx extensions, not part of Cafe OS ---

// Where OSReport & friends send their output. Default: stdout.
typedef void (*CoreinitNxLogSink)(const char *text, uint32_t size);
void coreinitNxSetLogSink(CoreinitNxLogSink sink);

// OSFatal never returns on Cafe OS: it shows an error screen and the user
// powers off. Default here: log the message and block forever. A port will
// want its own handler.
typedef void (*CoreinitNxFatalHandler)(const char *msg);
void coreinitNxSetFatalHandler(CoreinitNxFatalHandler handler);

#ifdef __cplusplus
}
#endif