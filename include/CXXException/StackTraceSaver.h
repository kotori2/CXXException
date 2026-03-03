//
// Created by kotori on 2022/6/12.
//

#ifndef CXXEXCEPTION_STACKTRACESAVER_H
#define CXXEXCEPTION_STACKTRACESAVER_H

#include "Defines.h"
#include "StackTrace.h"
#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>

namespace CXXException {
    class StackTraceSaver {
    private:
        struct LinkedList {
            std::shared_ptr<StackTrace> st;
            void *exception;  // COULD BE WILD POINTER
            LinkedList *next;
        };

        constexpr static int SAVE_SIZE = 100;
        LinkedList list_[StackTraceSaver::SAVE_SIZE]{};
        std::unordered_map<void *, LinkedList *> lookup_table_;
        std::mutex mutex_;
        LinkedList *available_head_ = &list_[0];

        StackTraceSaver() {
            for (int i = 0; i < SAVE_SIZE - 1; i++) {
                list_[i].next = &list_[i + 1];
            }
            list_[SAVE_SIZE - 1].next = &list_[0];
        }

    public:
        CXXEXCEPTION_API static StackTraceSaver& instance();

        CXXEXCEPTION_API void insert(void *exception, std::string_view exception_name);

        CXXEXCEPTION_API std::shared_ptr<StackTrace> retrieve(void *exception);
    };
}

#endif //CXXEXCEPTION_STACKTRACESAVER_H
