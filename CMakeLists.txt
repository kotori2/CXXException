cmake_minimum_required(VERSION 3.22)
project(CXXException)

set(CMAKE_CXX_STANDARD 20)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

set(CXXException_SRC
        StackTraceSaver.cpp
        StackTrace.cpp
        CXXHook.cpp)
set(CXXException_INCLUDE_DIR include)
add_executable(CXXException_test main.cpp ${CXXException_SRC})
add_library(CXXException_static STATIC ${CXXException_SRC})
add_library(CXXException_shared SHARED ${CXXException_SRC})
target_include_directories(CXXException_test PUBLIC ${CXXException_INCLUDE_DIR})
target_include_directories(CXXException_static PUBLIC ${CXXException_INCLUDE_DIR})
target_include_directories(CXXException_shared PUBLIC ${CXXException_INCLUDE_DIR})
set_target_properties(CXXException_static PROPERTIES OUTPUT_NAME CXXException)
set_target_properties(CXXException_shared PROPERTIES OUTPUT_NAME CXXException)
target_compile_definitions(CXXException_shared PRIVATE CXXEXCEPTION_SHARED_BUILD)