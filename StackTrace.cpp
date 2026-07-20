//
// Created by kotori on 2022/6/12.
//

#include <CXXException/StackTrace.h>

#ifdef WIN32
#include <Windows.h>
#include <winnt.h>

#include <vector>
#include <Psapi.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <iterator>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")

// Some imagehlp.dll versions lack their own packing directives.
#pragma pack( push, before_imagehlp, 8 )
#pragma pack( pop, before_imagehlp )

struct module_data {
    std::string image_name;
    std::string module_name;
    void *base_address{};
    DWORD load_size{};
};
typedef std::vector<module_data> ModuleList;

ModuleList load_modules_symbols( HANDLE hProcess, DWORD pid );

class SymHandler {
    HANDLE p;
public:
    explicit SymHandler(HANDLE process, char const *path = nullptr, bool intrude = false) : p(process) {
        if (!SymInitialize(p, path, intrude))
            throw(std::logic_error("Unable to initialize symbol handler"));
    }
    ~SymHandler() { SymCleanup(p); }
};

#ifdef _M_X64
STACKFRAME64 init_stack_frame(CONTEXT c) {
    STACKFRAME64 s;
    s.AddrPC.Offset = c.Rip;
    s.AddrPC.Mode = AddrModeFlat;
    s.AddrStack.Offset = c.Rsp;
    s.AddrStack.Mode = AddrModeFlat;
    s.AddrFrame.Offset = c.Rbp;
    s.AddrFrame.Mode = AddrModeFlat;
    return s;
}
#else
STACKFRAME64 init_stack_frame(CONTEXT c) {
    STACKFRAME64 s;
    s.AddrPC.Offset = c.Eip;
    s.AddrPC.Mode = AddrModeFlat;
    s.AddrStack.Offset = c.Esp;
    s.AddrStack.Mode = AddrModeFlat;
    s.AddrFrame.Offset = c.Ebp;
    s.AddrFrame.Mode = AddrModeFlat;
    return s;
}
#endif

void sym_options(DWORD add, DWORD remove=0) {
    DWORD symOptions = SymGetOptions();
    symOptions |= add;
    symOptions &= ~remove;
    SymSetOptions(symOptions);
}

class symbol {
    typedef IMAGEHLP_SYMBOL64 sym_type;
    sym_type *sym;
    static const int max_name_len = 1024;
public:
    symbol(HANDLE process, DWORD64 address) : sym((sym_type *)::operator new(sizeof(*sym) + max_name_len)) {
        memset(sym, 0, sizeof(*sym) + max_name_len);
        sym->SizeOfStruct = sizeof(*sym);
        sym->MaxNameLength = max_name_len;
        DWORD64 displacement;

        if (!SymGetSymFromAddr64(process, address, &displacement, sym)) {
            // throw std::logic_error("Bad symbol");
        }

    }

    [[maybe_unused]] std::string name() { return sym->Name; }
    std::string undecorated_name() {
        std::vector<char> und_name(max_name_len);
        UnDecorateSymbolName(sym->Name, &und_name[0], max_name_len, UNDNAME_COMPLETE);
        return {&und_name[0], strlen(&und_name[0])};
    }
};

class get_mod_info {
    HANDLE process;
    static const int buffer_length = 4096;
public:
    explicit get_mod_info(HANDLE h) : process(h) {}

    module_data operator()(HMODULE module) {
        module_data ret;
        char temp[buffer_length];
        MODULEINFO mi;

        GetModuleInformation(process, module, &mi, sizeof(mi));
        ret.base_address = mi.lpBaseOfDll;
        ret.load_size = mi.SizeOfImage;

        GetModuleFileNameEx(process, module, temp, sizeof(temp));
        ret.image_name = temp;
        GetModuleBaseName(process, module, temp, sizeof(temp));
        ret.module_name = temp;
        std::vector<char> img(ret.image_name.begin(), ret.image_name.end());
        std::vector<char> mod(ret.module_name.begin(), ret.module_name.end());
        SymLoadModule64(process, nullptr, &img[0], &mod[0], (DWORD64)ret.base_address, ret.load_size);
        return ret;
    }
};

ModuleList load_modules_symbols(HANDLE process, DWORD) {
    ModuleList modules;

    DWORD cbNeeded;
    std::vector<HMODULE> module_handles(1);

    EnumProcessModules(process, &module_handles[0], static_cast<DWORD>(module_handles.size()) * sizeof(HMODULE), &cbNeeded);
    module_handles.resize(cbNeeded/sizeof(HMODULE));
    EnumProcessModules(process, &module_handles[0], static_cast<DWORD>(module_handles.size()) * sizeof(HMODULE), &cbNeeded);

    std::transform(module_handles.begin(), module_handles.end(), std::back_inserter(modules), get_mod_info(process));
    return modules;
}

CXXException::StackTrace::StackTrace(std::string_view exception_name): exception_name_(exception_name) {
    HANDLE thread;

    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                    GetCurrentProcess(), &thread, 0, false, DUPLICATE_SAME_ACCESS);
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&ctx);
    try { parse_stack(thread, ctx); } catch (...) {}
    CloseHandle(thread);
}

bool CXXException::StackTrace::parse_stack(HANDLE hThread, CONTEXT &c) {
    HANDLE process = GetCurrentProcess();
    int frame_number = 0;
    SymHandler handler(process);
    sym_options(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    void *base = load_modules_symbols(process, GetCurrentProcessId())[0].base_address;

    STACKFRAME64 s = init_stack_frame(c);

    IMAGE_NT_HEADERS *h = ImageNtHeader(base);
    DWORD image_type = h->FileHeader.Machine;

    do {
        if (!StackWalk64(image_type, process, hThread, &s, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            return false;

        items_.push_back({
            frame_number,
            s
        });
        ++frame_number;
    } while (s.AddrReturn.Offset != 0);
    return true;
}

std::string CXXException::StackTrace::to_string() const {
    std::stringstream os;
    IMAGEHLP_LINE64 line = {0};
    line.SizeOfStruct = sizeof line;
    DWORD offset_from_symbol = 0;
    auto process = GetCurrentProcess();
    SymHandler handler(process, nullptr, true);

    auto modules = load_modules_symbols(process, GetCurrentProcessId());

#ifdef NDEBUG
    constexpr int skip_stacks = 3;
#else
    constexpr int skip_stacks = 5;
#endif

    const int skip = std::min(skip_stacks, static_cast<int>(items_.size()));
    for (auto i = items_.begin() + skip; i != items_.end(); ++i) {
        auto &&s = i->frame;
        if ( s.AddrPC.Offset != 0 ) {
            // find module
            module_data module{};
            for (const auto &m: modules) {
                auto base_address = reinterpret_cast<uintptr_t>(m.base_address);
                if (s.AddrPC.Offset > base_address && s.AddrPC.Offset < base_address + m.load_size) {
                    module = m;
                    break;
                }
            }
            // auto base_address = reinterpret_cast<uintptr_t>(module.base_address);
            os << "0x" << std::setfill('0') << std::setw(4) << std::hex << s.AddrPC.Offset << " ";
            os << "[" << module.module_name << "] ";
            os << std::dec << symbol(process, s.AddrPC.Offset).undecorated_name();
            if (SymGetLineFromAddr64( process, s.AddrPC.Offset, &offset_from_symbol, &line ) )
                os << "\t" << line.FileName << "(" << line.LineNumber << ")";
        } else {
            os << "(No Symbols: PC == 0)";
        }
        os << std::endl;
    }
    return os.str();
}
#else

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <execinfo.h>
#include <iomanip>
#include <new>
#include <sstream>
#include <string>

#include "SymbolResolver.h"

namespace CXXException {
    constexpr int PTR_SIZE = sizeof(void *);

    StackTrace::StackTrace(std::string_view exception_name) : exception_name_(exception_name) {
        constexpr int DEFAULT_SIZE = 20;
        constexpr int INCREASE_SIZE = 10;


        int current_size = DEFAULT_SIZE;
        void **last_frames = static_cast<void **>(malloc(current_size * PTR_SIZE));
        int last_size = backtrace(last_frames, current_size);

        while (current_size == last_size) {
            current_size += INCREASE_SIZE;
            last_frames = static_cast<void **>(realloc(last_frames, current_size * PTR_SIZE));
            if (!last_frames) throw std::bad_alloc();
            last_size = backtrace(last_frames, current_size);
        }

        for (int i = 0; i < last_size; i++) {
            items_.push_back({
                                     i + 1,
                                     last_frames[i]
                             });
        }
        free(last_frames);
    }

    std::string StackTrace::to_string() const {
        // Resolve all frames first; the names let us find and drop our own throw
        // machinery (the __cxa_throw hook and the StackTrace construction path).
        std::vector<detail::ResolvedFrame> resolved(items_.size());
        for (size_t i = 0; i < items_.size(); ++i)
            resolved[i] = detail::resolve_frame(items_[i].frame);

        // Anchor at the frame above our __cxa_throw hook (robust vs. a hard-coded
        // count); fall back to a fixed skip if the hook can't be named (stripped).
        size_t start = 0;
        bool anchored = false;
        for (size_t i = 0; i < resolved.size(); ++i) {
            if (resolved[i].function.find("cxa_throw") != std::string::npos) {
                start = i + 1;
                anchored = true;
            }
        }
        if (!anchored) {
#ifdef NDEBUG
            constexpr size_t skip_stacks = 3;
#else
            constexpr size_t skip_stacks = 10;
#endif
            start = std::min<size_t>(skip_stacks, items_.size());
        }

        std::ostringstream os;
        for (size_t i = start; i < items_.size(); ++i) {
            const detail::ResolvedFrame &rf = resolved[i];

            // module + 0xoffset from the module's load base: ASLR-stable. For ET_DYN
            // (PIE/shared) this is the addr2line/atos address; for non-PIE add the link base.
            os << "#" << std::setw(2) << std::setfill(' ') << (i - start) << "  "
               << (rf.module.empty() ? "?" : rf.module)
               << "+0x" << std::hex << rf.rel_addr << std::dec;
            if (!rf.function.empty())
                os << "  " << rf.function << " + " << rf.func_offset;
            os << "\n";
        }

        return os.str();
    }
}
#endif