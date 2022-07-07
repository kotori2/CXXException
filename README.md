# CXXException

A cross-platform C++ library to capture stacktrace from ANY exception.
Tested under Windows and macOS. 

## Usage
`CMakeLists.txt`
```cmake
add_subdirectory(external/CXXException)
target_link_libraries(${PROJECT_NAME} CXXException)
```
Obtain Exception: 
```c++
#include <CXXException/StackTraceSaver.h>

try {
    throw std::logic_error("anything");
} catch (std::exception &e) {
    auto st = CXXException::StackTraceSaver::instance()->retrieve(&e);
    std::cout << st->to_string() << std::endl;
}
```

## How does it work
Simply hooking exception throwing function in `libc`. 
Hook point: `__cxa_throw` on *nix and `_CxxThrowException` on Windows. 