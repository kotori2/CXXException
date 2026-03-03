//
// Created by kotori on 2022/6/12.
//

#ifdef WIN32
#include <Windows.h>
#include <Psapi.h>
#include <ehdata_forceinclude.h>
#include <CXXException/StackTraceSaver.h>

FARPROC SearchProcAddress(const char* func_name) {
    DWORD processID = GetCurrentProcessId();
    std::vector<HMODULE> hMods;
    DWORD cbNeeded;
    FARPROC result = nullptr;

    // Get a handle to the process.
    HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_READ,
                                   FALSE, processID );
    if (nullptr == hProcess) return nullptr;

    // Get a list_ of all the modules in this process.
    EnumProcessModules(hProcess, nullptr, 0, &cbNeeded);
    hMods.resize(cbNeeded);
    if(EnumProcessModules(hProcess, &hMods[0], static_cast<DWORD>(hMods.size()), &cbNeeded)) {
        for (const auto hMod: hMods) {
            wchar_t szModName[MAX_PATH];

            // Get the full path to the module's file.
            if (GetModuleFileNameW(hMod, szModName, sizeof(szModName) / sizeof(wchar_t))) {
                // wprintf( L"\t%ls (0x%08X)\n", szModName, hMod );
                if (wcsstr(szModName, L"VCRUNTIME") == nullptr) continue;
                // Print the module name and handle value.

                result = GetProcAddress(hMod, func_name);
                if (result) break;
            }
        }
    }
    // Release the handle to the process.
    CloseHandle( hProcess );

    return result;
}


extern "C" {
CXXEXCEPTION_API __declspec(noreturn) void __stdcall _CxxThrowException(void *pExceptionObject, _ThrowInfo *pThrowInfo) noexcept(false) {
    // std::cout << pExceptionObject << std::endl;
    CXXException::StackTraceSaver::instance().insert(pExceptionObject, "");
    static auto rethrow = (void (*)(void *, _ThrowInfo *)) SearchProcAddress("_CxxThrowException");
    rethrow(pExceptionObject, pThrowInfo);
}
}
#else
#include <string>
#include <typeinfo>
#include <cxxabi.h>
#include <dlfcn.h>
#include "include/CXXException/StackTraceSaver.h"

namespace {
    std::string demangle(const char *name) {
        int status;
        std::unique_ptr<char,void(*)(void*)> realname(abi::__cxa_demangle(name, nullptr, nullptr, &status), &std::free);
        return status ? "<demangle failed>" : &*realname;
    }
}

#if defined(__APPLE__) && defined(CXXEXCEPTION_SHARED_BUILD)
// macOS shared-library build: register cxa_throw as an interpose for
// __cxa_throw via the __DATA,__interpose section.  dyld patches all loaded
// images at startup, bypassing the two-level namespace without requiring
// -flat_namespace on any consumer binary.
//
// IMPORTANT: the interpose struct must be defined BEFORE cxa_throw so that
// cxa_throw can read its 'replacee' field to obtain the real libc++abi address.
// In dyld4 (macOS 12+), dlsym(RTLD_NEXT, "__cxa_throw") returns our own
// replacement rather than the original, making it unusable here.  The
// 'replacee' field is written at load-time fixup — before the interpose is
// applied — and therefore always holds the genuine libc++abi.__cxa_throw addr.

// __cxa_throw is declared in namespace __cxxabiv1 in <cxxabi.h> and is not
// visible at file scope by its plain C name; forward-declare it here.
extern "C" void __cxa_throw(void *, std::type_info *, void (*)(void *));
// Forward-declare cxa_throw so its address can appear in the struct initialiser.
extern "C" [[noreturn]] void cxa_throw(void *, std::type_info *, void (*)(void *));

// dyld-interposing.h was removed from the macOS SDK in Xcode 26; define the
// macro directly.  The __DATA,__interpose section is the stable ABI contract.
#define DYLD_INTERPOSE(_replacement, _replacee)                                \
    __attribute__((used)) static struct {                                      \
        const void *replacement; const void *replacee;                         \
    } _interpose_##_replacement                                                \
        __attribute__((section("__DATA,__interpose"))) = {                     \
            (const void *)(unsigned long)&(_replacement),                      \
            (const void *)(unsigned long)&(_replacee) };

DYLD_INTERPOSE(cxa_throw, __cxa_throw)
// _interpose_cxa_throw.replacee now holds the real libc++abi.__cxa_throw addr.
#endif

extern "C" {
[[noreturn]] void cxa_throw(void *ex, std::type_info *info, void (*dest)(void *)) {
    std::string exception_name = demangle(reinterpret_cast<const std::type_info*>(info)->name());
    CXXException::StackTraceSaver::instance().insert(ex, exception_name);

#if defined(__APPLE__) && defined(CXXEXCEPTION_SHARED_BUILD)
    // dyld4 redirects dlsym(RTLD_NEXT, "__cxa_throw") back to our replacement.
    // Use the interpose entry's replacee field instead: it was set at load-time
    // fixup (before interposing) to the real libc++abi.__cxa_throw address.
    static auto rethrow = reinterpret_cast<void (*)(void*,std::type_info *,void(*)(void*))>(
        (void*)_interpose_cxa_throw.replacee);
#else
    static auto rethrow = reinterpret_cast<void (*)(void*,std::type_info *,void(*)(void*))>(
        dlsym(RTLD_NEXT, "__cxa_throw"));
#endif
    rethrow(ex,info,dest);
    abort();  // make compiler happy
}

// On macOS shared builds DYLD_INTERPOSE is used (defined above), so the
// direct __cxa_throw definition is only needed for static builds and Linux.
#if !(defined(__APPLE__) && defined(CXXEXCEPTION_SHARED_BUILD))
#if !__clang__
CXXEXCEPTION_API void __cxa_throw(void *ex, void* info, void (*dest)(void*)) { cxa_throw(ex, reinterpret_cast<std::type_info *>(info), dest); }
#else
CXXEXCEPTION_API void __cxa_throw(void *ex, std::type_info* info, void (*dest)(void*)) { cxa_throw(ex, info, dest); }
#endif
#endif

}

#endif