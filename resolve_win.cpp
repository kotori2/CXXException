//
// Windows symbol resolver (dbghelp). Mirrors resolve_elf.cpp / resolve_macho.cpp:
// implements detail::resolve_frame. Module + base come from GetModuleHandleEx;
// the name/offset from SymGetSymFromAddr64 — the caller (StackTrace::to_string)
// holds the active dbghelp session (SymInitialize) via its SymHandler.
// NOTE: not compiled on this host (no Windows toolchain); verify on Windows CI.
//

#include "SymbolResolver.h"

#include <Windows.h>
#include <DbgHelp.h>

#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace CXXException::detail {
    namespace {
        std::string module_basename(void *pc, std::uintptr_t &base_out) {
            HMODULE hmod = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(pc), &hmod) || !hmod)
                return {};
            base_out = reinterpret_cast<std::uintptr_t>(hmod);

            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(hmod, path, MAX_PATH);
            std::wstring w(path);
            size_t slash = w.find_last_of(L"\\/");
            std::wstring name = slash == std::wstring::npos ? w : w.substr(slash + 1);
            return {name.begin(), name.end()};  // module names are ASCII
        }
    }

    ResolvedFrame resolve_frame(void *pc) {
        ResolvedFrame rf;
        rf.rel_addr = reinterpret_cast<std::uintptr_t>(pc);
        rf.func_offset = 0;
        if (!pc) return rf;

        std::uintptr_t base = 0;
        rf.module = module_basename(pc, base);
        if (base) rf.rel_addr = reinterpret_cast<std::uintptr_t>(pc) - base;

        constexpr int max_name = 1024;
        std::vector<char> buf(sizeof(IMAGEHLP_SYMBOL64) + max_name, 0);
        auto *sym = reinterpret_cast<IMAGEHLP_SYMBOL64 *>(buf.data());
        sym->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
        sym->MaxNameLength = max_name;

        DWORD64 disp = 0;
        if (SymGetSymFromAddr64(GetCurrentProcess(), reinterpret_cast<DWORD64>(pc), &disp, sym)) {
            std::vector<char> und(max_name);
            if (UnDecorateSymbolName(sym->Name, und.data(), max_name, UNDNAME_COMPLETE))
                rf.function = und.data();
            else
                rf.function = sym->Name;
            rf.func_offset = disp;
        }
        return rf;
    }
}
