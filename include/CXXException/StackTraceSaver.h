//
// Created by kotori on 2022/6/12.
//

#ifndef CXXEXCEPTION_STACKTRACESAVER_H
#define CXXEXCEPTION_STACKTRACESAVER_H

#include "StackTrace.h"
#include <atomic>
#include <mutex>
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

        static std::atomic<StackTraceSaver *>instance_;

        inline StackTraceSaver() {
            for (int i = 0; i < SAVE_SIZE - 1; i++) {
                list_[i].next = &list_[i + 1];
            }
            list_[SAVE_SIZE - 1].next = &list_[0];
        }

    public:
        static inline StackTraceSaver *instance() noexcept {
            if (!instance_) instance_ = new StackTraceSaver();
            return instance_;
        }

        void insert(void *exception, std::string_view exception_name);

        std::shared_ptr<StackTrace> retrieve(void *exception);
    };
}

#endif //CXXEXCEPTION_STACKTRACESAVER_H
