// Minimal test harness. OmniOS has no third-party dependencies yet and the ISO
// build should not need network access to run its own tests, so this is a
// self-registering CHECK/TEST pair rather than a vendored framework.
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace omnitest {

struct TestCase {
    std::string           name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

inline std::string& currentTest() {
    static std::string name;
    return name;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> body) {
        registry().push_back({std::move(name), std::move(body)});
    }
};

inline void reportFailure(const char* file, int line, const std::string& detail) {
    ++failureCount();
    std::cout << "  FAIL  " << currentTest() << "\n"
              << "        " << file << ":" << line << "\n"
              << "        " << detail << "\n";
}

inline int runAll() {
    int passed = 0;
    for (const TestCase& test : registry()) {
        currentTest() = test.name;
        const int before = failureCount();
        test.body();
        if (failureCount() == before) ++passed;
    }
    const int failed = static_cast<int>(registry().size()) - passed;
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << registry().size() << " total\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace omnitest

#define OMNI_CONCAT_INNER(a, b) a##b
#define OMNI_CONCAT(a, b) OMNI_CONCAT_INNER(a, b)

#define TEST(name)                                                            \
    static void OMNI_CONCAT(omniTestBody, __LINE__)();                        \
    static const ::omnitest::Registrar OMNI_CONCAT(omniTestReg, __LINE__)(    \
        name, OMNI_CONCAT(omniTestBody, __LINE__));                           \
    static void OMNI_CONCAT(omniTestBody, __LINE__)()

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition))                                                     \
            ::omnitest::reportFailure(__FILE__, __LINE__,                     \
                                      "expected: " #condition);               \
    } while (false)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        const auto& omniActual = (actual);                                    \
        const auto& omniExpected = (expected);                                \
        if (!(omniActual == omniExpected)) {                                  \
            std::ostringstream omniOut;                                       \
            omniOut << #actual " == " #expected "\n"                          \
                    << "          actual:   " << omniActual << "\n"           \
                    << "          expected: " << omniExpected;                \
            ::omnitest::reportFailure(__FILE__, __LINE__, omniOut.str());     \
        }                                                                     \
    } while (false)
