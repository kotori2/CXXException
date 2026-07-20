//
// Internal POSIX symbol-resolution layer. Each platform implements resolve_frame()
// in its own TU: Linux -> resolve_elf.cpp, macOS -> resolve_macho.cpp. Windows
// keeps its own dbghelp path in StackTrace.cpp. Internal header, not public API.
//

#ifndef CXXEXCEPTION_SYMBOLRESOLVER_H
#define CXXEXCEPTION_SYMBOLRESOLVER_H

#include <cstdint>
#include <cstdlib>
#include <string>

namespace CXXException::detail {

    struct ResolvedFrame {
        std::string module;          // module basename; empty if unknown
        std::string function;        // demangled name; empty if unknown
        std::uintptr_t rel_addr;     // offset from module load base (ASLR-stable); raw pc if unknown
        std::uintptr_t func_offset;  // offset within `function` (0 when function empty)
    };

    // Implemented per-platform (resolve_elf.cpp / resolve_macho.cpp / resolve_win.cpp).
    ResolvedFrame resolve_frame(void *pc);

}

#ifndef _WIN32
#include <cxxabi.h>
namespace CXXException::detail {
    // Demangled name, or the original string on failure (empty if null).
    // Windows demangles via UnDecorateSymbolName in resolve_win.cpp instead.
    inline std::string demangle(const char *mangled) {
        if (!mangled) return {};
        int status = 0;
        char *out = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
        std::string result = (status == 0 && out) ? out : mangled;
        std::free(out);
        return result;
    }
}
#endif

#endif //CXXEXCEPTION_SYMBOLRESOLVER_H
