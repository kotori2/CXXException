//
// Created by kotori on 2022/6/12.
//

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#include <vector>
// Returns the address of func_name from any loaded DLL other than our own module.
static FARPROC SearchProcAddress(const char *func_name) {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&SearchProcAddress), &self);
    DWORD cb = 0;
    EnumProcessModules(GetCurrentProcess(), nullptr, 0, &cb);
    std::vector<HMODULE> mods(cb / sizeof(HMODULE));
    EnumProcessModules(GetCurrentProcess(), mods.data(), cb, &cb);
    for (HMODULE m : mods) {
        if (m == self) continue;
        if (FARPROC fn = GetProcAddress(m, func_name)) return fn;
    }
    return nullptr;
}
#endif

#ifdef _MSC_VER
#ifdef __clang__
// clang-cl doesn't pre-inject _ThrowInfo; forward-declare it.
struct _ThrowInfo;
#endif
#include <ehdata_forceinclude.h>
#include <CXXException/StackTraceSaver.h>


// No __declspec(dllexport) here: extern "C" + dllexport on _CxxThrowException
// triggers C2375; the shared build exports it via CXXException.def instead.
extern "C" {
__declspec(noreturn) void __stdcall _CxxThrowException(void *pExceptionObject, _ThrowInfo *pThrowInfo) noexcept(false) {
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
#include <CXXException/StackTraceSaver.h>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace {
    std::string demangle(const char *name) {
        int status;
        std::unique_ptr<char,void(*)(void*)> realname(abi::__cxa_demangle(name, nullptr, nullptr, &status), &std::free);
        return status ? "<demangle failed>" : &*realname;
    }
}

#if defined(__APPLE__) && defined(CXXEXCEPTION_SHARED_BUILD)
// Interpose cxa_throw over __cxa_throw via __DATA,__interpose: dyld patches all
// images at load, bypassing the two-level namespace with no consumer flags. The
// struct must precede cxa_throw, which reads its 'replacee' field for the real
// libc++abi address — under dyld4 dlsym(RTLD_NEXT, "__cxa_throw") returns our hook.

// Forward-declare __cxa_throw (namespace-scoped in <cxxabi.h>). 2nd param:
// libstdc++ uses void*, libc++abi uses std::type_info*.
#if __clang__
extern "C" void __cxa_throw(void *, std::type_info *, void (*)(void *));
#else
extern "C" void __cxa_throw(void *, void *, void (*)(void *));
#endif
// Forward-declare cxa_throw for the interpose struct initialiser.
extern "C" [[noreturn]] void cxa_throw(void *, std::type_info *, void (*)(void *));

// mach-o/dyld-interposing.h was removed in Xcode 26; define the macro inline.
#define DYLD_INTERPOSE(_replacement, _replacee)                                \
    __attribute__((used)) static struct {                                      \
        const void *replacement; const void *replacee;                         \
    } _interpose_##_replacement                                                \
        __attribute__((section("__DATA,__interpose"))) = {                     \
            (const void *)(unsigned long)&(_replacement),                      \
            (const void *)(unsigned long)&(_replacee) };

DYLD_INTERPOSE(cxa_throw, __cxa_throw)
#endif

extern "C" {
[[noreturn]] void cxa_throw(void *ex, std::type_info *info, void (*dest)(void *)) {
    std::string exception_name = demangle(reinterpret_cast<const std::type_info*>(info)->name());
    CXXException::StackTraceSaver::instance().insert(ex, exception_name);

#if defined(__APPLE__) && defined(CXXEXCEPTION_SHARED_BUILD)
    // replacee holds the real libc++abi.__cxa_throw (dlsym(RTLD_NEXT) returns our hook).
    static auto rethrow = reinterpret_cast<void (*)(void*,std::type_info *,void(*)(void*))>(
        (void*)_interpose_cxa_throw.replacee);
#elif defined(_WIN32)
    // MinGW: GCC uses void* for the type_info parameter in __cxa_throw.
    static auto rethrow = reinterpret_cast<void (*)(void*,void*,void(*)(void*))>(
        SearchProcAddress("__cxa_throw"));
#else
    static auto rethrow = reinterpret_cast<void (*)(void*,std::type_info *,void(*)(void*))>(
        dlsym(RTLD_NEXT, "__cxa_throw"));
#endif
    rethrow(ex,info,dest);
    abort();  // make compiler happy
}

// Static builds & Linux define __cxa_throw directly (macOS shared uses the interpose).
#if !(defined(__APPLE__) && defined(CXXEXCEPTION_SHARED_BUILD))
#if !__clang__
CXXEXCEPTION_API void __cxa_throw(void *ex, void* info, void (*dest)(void*)) { cxa_throw(ex, reinterpret_cast<std::type_info *>(info), dest); }
#else
CXXEXCEPTION_API void __cxa_throw(void *ex, std::type_info* info, void (*dest)(void*)) { cxa_throw(ex, info, dest); }
#endif
#endif

}

#endif