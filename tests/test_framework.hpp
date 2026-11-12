#pragma once

// Minimal self-contained test framework (no external dependencies).
//
//   TEST(name) { CHECK(cond); REQUIRE(cond); CHECK_EQ(a, b); }
//   int main() { return lob::test::run_all(); }

#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace lob::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct RequireFailed : std::exception {};

inline int& failures() {
    static int n = 0;
    return n;
}

inline void report_failure(const char* expr, const char* file, int line, const std::string& detail) {
    ++failures();
    std::fprintf(stderr, "    FAILED: %s\n      at %s:%d%s%s\n", expr, file, line,
                 detail.empty() ? "" : "\n      ", detail.c_str());
}

template <typename A, typename B>
std::string describe_eq(const A& a, const B& b) {
    std::ostringstream os;
    os << "lhs = " << a << ", rhs = " << b;
    return os.str();
}

inline int run_all() {
    int passed = 0;
    for (const TestCase& t : registry()) {
        const int before = failures();
        std::fprintf(stderr, "[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const RequireFailed&) {
            // already reported
        } catch (const std::exception& e) {
            report_failure("unexpected exception", __FILE__, __LINE__, e.what());
        }
        if (failures() == before) {
            ++passed;
            std::fprintf(stderr, "[  OK  ] %s\n", t.name);
        } else {
            std::fprintf(stderr, "[ FAIL ] %s\n", t.name);
        }
    }
    const int total = static_cast<int>(registry().size());
    std::fprintf(stderr, "\n%d/%d tests passed, %d check(s) failed\n", passed, total, failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace lob::test

#define LOB_TEST_CONCAT_(a, b) a##b
#define LOB_TEST_CONCAT(a, b) LOB_TEST_CONCAT_(a, b)

#define TEST(name)                                                             \
    static void name();                                                        \
    static ::lob::test::Registrar LOB_TEST_CONCAT(registrar_, name)(#name, &name); \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) ::lob::test::report_failure(#cond, __FILE__, __LINE__, ""); \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        const auto& lob_a_ = (a);                                              \
        const auto& lob_b_ = (b);                                              \
        if (!(lob_a_ == lob_b_))                                               \
            ::lob::test::report_failure(#a " == " #b, __FILE__, __LINE__,      \
                                        ::lob::test::describe_eq(lob_a_, lob_b_)); \
    } while (0)

#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::lob::test::report_failure(#cond, __FILE__, __LINE__, "(required)"); \
            throw ::lob::test::RequireFailed{};                                \
        }                                                                      \
    } while (0)
