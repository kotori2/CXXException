//
// Created by kotori on 2022/6/12.
//

#ifndef CXXEXCEPTION_DEFINES_H
#define CXXEXCEPTION_DEFINES_H

#if defined(CXXEXCEPTION_SHARED_BUILD)
  #if defined(_WIN32)
    #define CXXEXCEPTION_API __declspec(dllexport)
  #else
    #define CXXEXCEPTION_API __attribute__((visibility("default")))
  #endif
#else
  #define CXXEXCEPTION_API
#endif

#endif //CXXEXCEPTION_DEFINES_H
