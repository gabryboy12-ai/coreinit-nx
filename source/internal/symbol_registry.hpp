#pragma once
#include <cstddef>

namespace coreinit_nx {

// Maps ("coreinit", "OSGetTime") to the address of our implementation.
//
// Games resolve some functions by name at runtime through OSDynLoad. They
// are not loading unknown code -- they acquire the SYSTEM RPLs, which are
// exactly what this library implements. So no loader is needed: just a
// table of what we export.
//
// A useful side effect: this registry is the authoritative list of the
// library's public surface, more reliable than the hand-maintained
// implemented.txt.
enum class ExportKind { Function, Data };

struct ExportedSymbol {
    const char *library;
    const char *name;
    void       *address;
    ExportKind  kind;
};

void *findExport(const char *library, const char *name);
void *findExportOfKind(const char *library, const char *name, ExportKind kind);

// True if we implement anything from this library at all.
bool hasLibrary(const char *library);

size_t exportCount();

} // namespace coreinit_nx