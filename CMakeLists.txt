cmake_minimum_required(VERSION 3.22)
project(CXXException)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

set(CXXException_SRC
        StackTraceSaver.cpp
        StackTrace.cpp
        CXXHook.cpp)
# *nix symbol resolution is split per-ABI into its own translation unit; pick the
# one matching the platform. Windows keeps its dbghelp path inside StackTrace.cpp.
if(UNIX AND NOT APPLE)
    list(APPEND CXXException_SRC resolve_elf.cpp)
elseif(APPLE)
    list(APPEND CXXException_SRC resolve_macho.cpp)
endif()
set(CXXException_INCLUDE_DIR include)
add_executable(CXXException_example main.cpp ${CXXException_SRC})
add_library(CXXException_static STATIC ${CXXException_SRC})
add_library(CXXException_shared SHARED ${CXXException_SRC})
target_include_directories(CXXException_example PUBLIC ${CXXException_INCLUDE_DIR})
target_include_directories(CXXException_static PUBLIC ${CXXException_INCLUDE_DIR})
target_include_directories(CXXException_shared PUBLIC ${CXXException_INCLUDE_DIR})
set_target_properties(CXXException_static PROPERTIES OUTPUT_NAME CXXException)
set_target_properties(CXXException_shared PROPERTIES
        OUTPUT_NAME CXXException
        ARCHIVE_OUTPUT_NAME CXXException_imp)
target_compile_definitions(CXXException_shared PRIVATE CXXEXCEPTION_SHARED_BUILD)
# _CxxThrowException can't carry dllexport (C2375); export it via the .def file.
if(WIN32)
    target_sources(CXXException_shared PRIVATE CXXException.def)
endif()

# MinGW ignores #pragma comment(lib,...); link psapi/dbghelp explicitly
# (MSVC handles these via #pragma comment).
if(WIN32 AND NOT MSVC)
    foreach(_tgt CXXException_static CXXException_shared CXXException_example)
        target_link_libraries(${_tgt} PUBLIC psapi dbghelp)
    endforeach()
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

# Static-linkage test: CXXHook object is baked into the executable, so
# __cxa_throw / _CxxThrowException defined there overrides the runtime's.
add_executable(test_static tests/test_basic.cpp)
target_include_directories(test_static PRIVATE ${CXXException_INCLUDE_DIR})
target_link_libraries(test_static PRIVATE CXXException_static)
if(UNIX AND NOT APPLE)
    # backtrace/dladdr need -ldl on Linux (folded into libc on glibc >= 2.34).
    target_link_libraries(test_static PRIVATE dl)
endif()
add_test(NAME static_tests COMMAND test_static)

# Shared-linkage test: the dylib/DLL must shadow the runtime's __cxa_throw /
# _CxxThrowException — macOS via DYLD_INTERPOSE, Linux via link order, Windows by
# linking our import lib before vcruntime.
add_executable(test_shared tests/test_basic.cpp)
target_include_directories(test_shared PRIVATE ${CXXException_INCLUDE_DIR})
target_link_libraries(test_shared PRIVATE CXXException_shared)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_shared PRIVATE dl)
endif()
add_test(NAME shared_tests COMMAND test_shared)