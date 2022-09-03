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
add_library(CXXException ${CXXException_SRC})
target_include_directories(CXXException_test PUBLIC ${CXXException_INCLUDE_DIR})
target_include_directories(CXXException PUBLIC ${CXXException_INCLUDE_DIR})