//
// macOS symbol resolver via dladdr, which reads the Mach-O symbol table
// (LC_SYMTAB) incl. static/local symbols when unstripped — no hand-parsing needed.
// Contingency (see KNOWN_ISSUES.md): parse LC_SYMTAB (nlist_64) directly, mirroring
// resolve_elf.cpp, if dladdr's lack of bounds checking proves a problem.
//

#include "SymbolResolver.h"

#include <dlfcn.h>
#include <cstring>

namespace CXXException::detail {
    namespace {
        const char *basename_of(const char *path) {
            const char *slash = std::strrchr(path, '/');
            return slash ? slash + 1 : path;
        }
    }

    ResolvedFrame resolve_frame(void *pc) {
        ResolvedFrame rf;
        const auto addr = reinterpret_cast<std::uintptr_t>(pc);
        rf.rel_addr = addr;   // until the module (and its base) is known
        rf.func_offset = 0;
        // Return addresses: resolve pc-1 (the call site), else a noreturn call at a
        // function's tail (e.g. throw) misattributes to the next symbol.
        const auto lookup = addr ? addr - 1 : addr;

        Dl_info info;
        if (!dladdr(reinterpret_cast<void *>(lookup), &info) || !info.dli_fname) {
            return rf;  // module unknown
        }

        rf.module = basename_of(info.dli_fname);
        rf.rel_addr = addr - reinterpret_cast<std::uintptr_t>(info.dli_fbase);

        if (info.dli_sname && info.dli_sname[0]) {
            rf.function = demangle(info.dli_sname);
            if (info.dli_saddr)
                rf.func_offset = addr - reinterpret_cast<std::uintptr_t>(info.dli_saddr);
        }
        return rf;
    }

}
