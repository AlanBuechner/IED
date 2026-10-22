// A tiny test runner: TEST(name) registers a test, CHECK and CHECK_EQ record failures, RunAll runs them all.
#pragma once

#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    void (*run)();
};

std::vector<Case>& Registry();
void Fail(const char* file, int line, const std::string& message);

struct Registration {
    Registration(const char* name, void (*run)()) { Registry().push_back(Case{name, run}); }
};

// Runs every registered test; the number of failed checks.
int RunAll();

}  // namespace check

#define TEST(name)                                                    \
    static void name();                                               \
    static const check::Registration name##Registration(#name, name); \
    static void name()

#define CHECK(condition) \
    ((condition) ? void() : check::Fail(__FILE__, __LINE__, "CHECK(" #condition ")"))

#define CHECK_EQ(expected, actual)                                                                          \
    do {                                                                                                    \
        const std::string expectedText(expected), actualText(actual);                                       \
        if (expectedText != actualText)                                                                     \
            check::Fail(__FILE__, __LINE__, "expected [" + expectedText + "] got [" + actualText + "]"); \
    } while (false)
