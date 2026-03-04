cmake_minimum_required(VERSION 3.22)
project(CXXException)

set(CMAKE_CXX_STANDARD 20)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/utf-8)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

set(CXXException_SRC
        StackTraceSaver.cpp
        StackTrace.cpp
        CXXHook.cpp)
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
# On Windows, _CxxThrowException cannot carry __declspec(dllexport) without
# triggering C2375 (conflicts with the compiler's internal C++ declaration).
# Export it via a module definition file instead.
if(WIN32)
    target_sources(CXXException_shared PRIVATE CXXException.def)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

# Static-linkage test: CXXHook object is baked into the executable, so
# __cxa_throw / _CxxThrowException defined there overrides the runtime's.
add_executable(test_static tests/test_basic.cpp)
target_include_directories(test_static PRIVATE ${CXXException_INCLUDE_DIR})
target_link_libraries(test_static PRIVATE CXXException_static)
if(UNIX AND NOT APPLE)
    # backtrace/dladdr live in libdl on Linux (in libc on glibc >= 2.34, but
    # linking -ldl is always safe).
    target_link_libraries(test_static PRIVATE dl)
endif()
add_test(NAME static_tests COMMAND test_static)

# Shared-linkage test: the dylib/DLL exports __cxa_throw / _CxxThrowException
# and must shadow the runtime's symbol.
#
#  macOS  – DYLD_INTERPOSE in the dylib patches all images at load time;
#            no consumer-side flags are needed.
#  Linux  – flat namespace by default; explicit link order is sufficient.
#  Windows – CMake links our import library before the default vcruntime.lib,
#            so _CxxThrowException resolves to CXXException.dll at link time.
add_executable(test_shared tests/test_basic.cpp)
target_include_directories(test_shared PRIVATE ${CXXException_INCLUDE_DIR})
target_link_libraries(test_shared PRIVATE CXXException_shared)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_shared PRIVATE dl)
endif()
add_test(NAME shared_tests COMMAND test_shared)