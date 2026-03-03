#include <CXXException/StackTraceSaver.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>

// Prints [FAIL] and returns false so the caller can tally failures without
// aborting immediately — every test runs even if an earlier one fails.
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "[FAIL] " << __func__ << ": " #cond                  \
                      << "  (" __FILE__ ":" << __LINE__ << ")\n";             \
            return false;                                                      \
        }                                                                      \
    } while (0)

// ── helpers ──────────────────────────────────────────────────────────────────

static CXXException::StackTraceSaver& saver() {
    return CXXException::StackTraceSaver::instance();
}

// ── test cases ───────────────────────────────────────────────────────────────

// Throw a standard exception; retrieve() must return a non-null trace with
// non-empty string output.
bool test_basic_retrieve() {
    try {
        throw std::exception();
    } catch (std::exception& e) {
        auto st = saver().retrieve(&e);
        CHECK(st != nullptr);
        CHECK(!st->to_string().empty());
    }
    std::cout << "[PASS] " << __func__ << "\n";
    return true;
}

// An exception object that was never thrown must yield nullptr.
bool test_unknown_pointer_returns_null() {
    std::exception local;
    auto st = saver().retrieve(&local);
    CHECK(st == nullptr);
    std::cout << "[PASS] " << __func__ << "\n";
    return true;
}

// Two separate throw sites produce two distinct, non-null stack traces.
bool test_multiple_exceptions() {
    std::shared_ptr<CXXException::StackTrace> st1, st2;

    try {
        throw std::runtime_error("first");
    } catch (std::runtime_error& e) {
        st1 = saver().retrieve(&e);
        CHECK(st1 != nullptr);
    }

    try {
        throw std::logic_error("second");
    } catch (std::logic_error& e) {
        st2 = saver().retrieve(&e);
        CHECK(st2 != nullptr);
    }

    CHECK(st1 != st2);
    std::cout << "[PASS] " << __func__ << "\n";
    return true;
}

// After a bare `throw;` rethrow the exception pointer is unchanged, so the
// original stack trace must still be retrievable.
static void rethrow_thrower() {
    throw std::runtime_error("rethrow");
}

bool test_rethrow_preserves_trace() {
    try {
        try {
            rethrow_thrower();
        } catch (std::runtime_error& e) {
            CHECK(saver().retrieve(&e) != nullptr);
            throw;   // rethrow — same object, same address
        }
    } catch (std::runtime_error& e) {
        auto st = saver().retrieve(&e);
        CHECK(st != nullptr);
        CHECK(!st->to_string().empty());
    }
    std::cout << "[PASS] " << __func__ << "\n";
    return true;
}

// A user-defined exception type (not from <stdexcept>) must also be captured.
struct MyException : std::exception {
    const char* what() const noexcept override { return "MyException"; }
};

bool test_custom_exception_type() {
    try {
        throw MyException{};
    } catch (MyException& e) {
        auto st = saver().retrieve(&e);
        CHECK(st != nullptr);
        CHECK(!st->to_string().empty());
    }
    std::cout << "[PASS] " << __func__ << "\n";
    return true;
}

// Throw from inside a deep call chain; the captured trace must be non-empty.
static void deep3() { throw std::runtime_error("deep"); }
static void deep2() { deep3(); }
static void deep1() { deep2(); }

bool test_deep_call_stack() {
    try {
        deep1();
    } catch (std::runtime_error& e) {
        auto st = saver().retrieve(&e);
        CHECK(st != nullptr);
        CHECK(!st->to_string().empty());
    }
    std::cout << "[PASS] " << __func__ << "\n";
    return true;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    int failed = 0;

    if (!test_basic_retrieve())              ++failed;
    if (!test_unknown_pointer_returns_null()) ++failed;
    if (!test_multiple_exceptions())          ++failed;
    if (!test_rethrow_preserves_trace())      ++failed;
    if (!test_custom_exception_type())        ++failed;
    if (!test_deep_call_stack())              ++failed;

    if (failed == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cerr << "\n" << failed << " test(s) failed.\n";
    return 1;
}
