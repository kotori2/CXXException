//
// Created by kotori on 2022/6/12.
//

#ifndef CXXEXCEPTION_STACKTRACE_H
#define CXXEXCEPTION_STACKTRACE_H

#include <functional>
#include <string>
#include <vector>
#ifdef WIN32
#include <Windows.h>
#include <ImageHlp.h>
#else

#endif
namespace CXXException {
    class StackTrace {
        struct StackTraceItem {
            int32_t frame_number;
#ifdef WIN32
            STACKFRAME64 frame;
#else
            void *frame;
#endif
        };
    private:
        std::vector<StackTraceItem> items_;
        std::string exception_name_;
#ifdef WIN32
        bool parse_stack(HANDLE hThread, CONTEXT &c);
#endif
    public:
        explicit StackTrace(std::string_view exception_name);

        std::string to_string();
    };
}


#endif //CXXEXCEPTION_STACKTRACE_H
