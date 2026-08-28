// Minimal test framework for CBase.
//
// Deliberately tiny: TEST() registers a function, CHECK/CHECK_EQ report
// failures with file/line, RUN_TESTS() generates the main() that runs
// everything and returns non-zero on failure. No external dependency -
// this compiles with the same single g++ invocation as the project itself.

#pragma once
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace ctest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int& failure_count() {
    static int f = 0;
    return f;
}

inline int& check_count() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, fn});
    }
};

template <class T>
std::string str(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

inline void report(bool ok, const std::string& expr,
                   const std::string& detail,
                   const char* file, int line) {
    ++check_count();
    if (!ok) {
        ++failure_count();
        std::printf("      FAIL %s:%d  %s\n", file, line, expr.c_str());
        if (!detail.empty()) {
            std::printf("           got:      %s\n", detail.c_str());
        }
    }
}

inline int run_all(const char* suite) {
    std::printf("=== %s ===\n", suite);
    int failed_tests = 0;
    for (const TestCase& t : registry()) {
        int before = failure_count();
        std::printf("[ RUN  ] %s\n", t.name.c_str());
        t.fn();
        if (failure_count() > before) {
            ++failed_tests;
            std::printf("[ FAIL ] %s\n", t.name.c_str());
        } else {
            std::printf("[  OK  ] %s\n", t.name.c_str());
        }
    }
    std::printf("=== %s: %d/%d test(s) passed, %d/%d check(s) passed ===\n",
                suite, (int)registry().size() - failed_tests,
                (int)registry().size(),
                check_count() - failure_count(), check_count());
    return failed_tests == 0 ? 0 : 1;
}

// RAII temp database file: removed on construction AND destruction so a
// crashed run cannot poison the next one.
struct TmpFile {
    std::string path;
    explicit TmpFile(const std::string& tag) {
        static int counter = 0;
        std::ostringstream os;
        os << "cbase_test_" << tag << "_" << ++counter << ".tmp";
        path = os.str();
        std::remove(path.c_str());
    }
    ~TmpFile() { std::remove(path.c_str()); }
};

}  // namespace ctest

#define TEST(name)                                                        \
    static void ctest_fn_##name();                                        \
    static ctest::Registrar ctest_reg_##name(#name, ctest_fn_##name);     \
    static void ctest_fn_##name()

#define CHECK(expr) \
    ctest::report((expr), #expr, "", __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        auto va_ = (a);                                                    \
        auto vb_ = (b);                                                    \
        ctest::report(va_ == vb_, #a " == " #b,                            \
                      ctest::str(va_) + " != " + ctest::str(vb_),          \
                      __FILE__, __LINE__);                                 \
    } while (0)

#define RUN_TESTS() \
    int main() { return ctest::run_all(__FILE__); }
