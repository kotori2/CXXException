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
    if( EnumProcessModules(hProcess, &hMods[0], hMods.size(), &cbNeeded)) {
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
void __stdcall _CxxThrowException(void *pExceptionObject, _ThrowInfo *pThrowInfo) {
    auto* pTI = (ThrowInfo*)pThrowInfo;

    // std::cout << pExceptionObject << std::endl;
    CXXException::StackTraceSaver::instance()->insert(pExceptionObject, "");
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
        std::unique_ptr<char,void(*)(void*)> realname(abi::__cxa_demangle(name, 0, 0, &status), &std::free);
        return status ? "<demangle failed>" : &*realname;
    }
}

extern "C" {
void __cxa_throw(void *ex, std::type_info *info, void (*dest)(void *)) {
    std::string exception_name = demangle(reinterpret_cast<const std::type_info*>(info)->name());
    CXXException::StackTraceSaver::instance()->insert(ex, exception_name);

    static auto rethrow = reinterpret_cast<void (*)(void*,std::type_info *,void(*)(void*))>(dlsym(RTLD_NEXT, "__cxa_throw"));
    rethrow(ex,info,dest);
}
}
#endif