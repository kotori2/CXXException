//
// Created by kotori on 2022/6/12.
//

#include <CXXException/StackTraceSaver.h>


namespace CXXException {
    std::atomic<StackTraceSaver *> StackTraceSaver::instance_ = nullptr;

    void StackTraceSaver::insert(void *exception, std::string_view exception_name) {
        std::lock_guard lg(mutex_);
        if (available_head_->exception) lookup_table_.erase(exception);
        available_head_->exception = exception;
        available_head_->st = std::make_shared<StackTrace>(exception_name);
        lookup_table_[exception] = available_head_;
        available_head_ = available_head_->next;
    }

    std::shared_ptr<StackTrace> StackTraceSaver::retrieve(void *exception) {
        std::lock_guard lg(mutex_);
        auto item = lookup_table_.find(exception);
        if (item == lookup_table_.end()) {
            return nullptr;
        }
        return item->second->st;
    }
}