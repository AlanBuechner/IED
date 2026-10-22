#pragma once

#include "TestBase.h"
#include <cmath>
#include <sstream>
#include <type_traits>

namespace Testing
{
    // ─── Internal helpers ──────────────────────────────────────────────────

    /// Evaluate a condition and report a failure if it is false.
    template <typename ... Args>
    static void AssertTrueImpl(bool condition, const char* expr, const char* file, int line,
                                const Core::String& msg, const Args&... args)
    {
        if (!condition)
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr << "\" failed";
            if (sizeof...(args) > 0)
                ss << ": " << Core::StringFormat::Format(msg.c_str(), args...);
            else if (!msg.empty())
                ss << ": " << msg;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Equality assertion with type-safe mismatch reporting.
    template <typename T>
    static void AssertEqualImpl(const T& actual, const T& expected, const char* expr,
                                 const char* file, int line)
    {
        static_assert(std::is_convertible_v<decltype(actual == expected), bool>,
                      "TEST_ASSERT_EQ requires operator== between the two operand types.");

        if (!(actual == expected))
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: actual = " << actual << ", expected = " << expected;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Floating-point equality with configurable epsilon.
    template <typename T>
    static void AssertFloatEqualImpl(T actual, T expected, T epsilon, const char* expr,
                                      const char* file, int line)
    {
        static_assert(std::is_floating_point_v<std::decay_t<T>>,
                      "TEST_ASSERT_FLOAT_EQ requires floating-point types.");

        if (std::abs(actual - expected) > epsilon)
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: actual = " << actual << ", expected = " << expected
               << ", epsilon = " << epsilon;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Not-equal assertion.
    template <typename T>
    static void AssertNotEqualImpl(const T& actual, const T& notExpected, const char* expr,
                                    const char* file, int line)
    {
        static_assert(std::is_convertible_v<decltype(actual == notExpected), bool>,
                      "TEST_ASSERT_NE requires operator== between the two operand types.");

        if (actual == notExpected)
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: actual unexpectedly equals " << notExpected;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Strict less-than ordering.
    template <typename T>
    static void AssertLessThanImpl(const T& a, const T& b, const char* expr,
                                    const char* file, int line)
    {
        static_assert(std::is_convertible_v<decltype(a < b), bool>,
                      "TEST_ASSERT_LT requires operator< between the two operand types.");

        if (!(a < b))
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: " << a << " is not less than " << b;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Less-than-or-equal ordering.
    template <typename T>
    static void AssertLessEqualImpl(const T& a, const T& b, const char* expr,
                                     const char* file, int line)
    {
        static_assert(std::is_convertible_v<decltype(a <= b), bool>,
                      "TEST_ASSERT_LE requires operator<= between the two operand types.");

        if (!(a <= b))
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: " << a << " is not less than or equal to " << b;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Strict greater-than ordering.
    template <typename T>
    static void AssertGreaterThanImpl(const T& a, const T& b, const char* expr,
                                       const char* file, int line)
    {
        static_assert(std::is_convertible_v<decltype(a > b), bool>,
                      "TEST_ASSERT_GT requires operator> between the two operand types.");

        if (!(a > b))
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: " << a << " is not greater than " << b;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Greater-than-or-equal ordering.
    template <typename T>
    static void AssertGreaterEqualImpl(const T& a, const T& b, const char* expr,
                                        const char* file, int line)
    {
        static_assert(std::is_convertible_v<decltype(a >= b), bool>,
                      "TEST_ASSERT_GE requires operator>= between the two operand types.");

        if (!(a >= b))
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: " << a << " is not greater than or equal to " << b;

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Null pointer assertion.
    template <typename T>
    static void AssertNullImpl(T ptr, const char* expr, const char* file, int line)
    {
        if (ptr != nullptr)
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: pointer is not null (" << ptr << ")";

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }

    /// Non-null pointer assertion.
    template <typename T>
    static void AssertNotNullImpl(T ptr, const char* expr, const char* file, int line)
    {
        if (ptr == nullptr)
        {
            std::stringstream ss;
            ss << file << "(" << line << "): assertion \"" << expr
               << "\" failed: pointer is null";

            Test::ReportFailure(ss.str());
            throw TestFailedException(ss.str());
        }
    }
}

// ─── Public Assertion Macros ──────────────────────────────────────────────

/// Assert that a boolean condition is true.
#define TEST_ASSERT(condition) \
    Testing::AssertTrueImpl(static_cast<bool>(condition), #condition, __FILE__, __LINE__, "")

/// Assert that a boolean condition is true, with optional formatted message.
#define TEST_ASSERT_MSG(condition, ...) \
    Testing::AssertTrueImpl(static_cast<bool>(condition), #condition, __FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)

/// Assert that two values are equal (type-safe via operator==).
#define TEST_ASSERT_EQ(actual, expected) \
    Testing::AssertEqualImpl((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)

/// Assert that two floating-point values are within epsilon.
#define TEST_ASSERT_FLOAT_EQ(actual, expected, epsilon) \
    Testing::AssertFloatEqualImpl((actual), (expected), (epsilon), #actual " == " #expected, __FILE__, __LINE__)

/// Assert that two values are not equal.
#define TEST_ASSERT_NE(actual, notExpected) \
    Testing::AssertNotEqualImpl((actual), (notExpected), #actual " != " #notExpected, __FILE__, __LINE__)

/// Assert that a < b.
#define TEST_ASSERT_LT(a, b) \
    Testing::AssertLessThanImpl((a), (b), #a " < " #b, __FILE__, __LINE__)

/// Assert that a <= b.
#define TEST_ASSERT_LE(a, b) \
    Testing::AssertLessEqualImpl((a), (b), #a " <= " #b, __FILE__, __LINE__)

/// Assert that a > b.
#define TEST_ASSERT_GT(a, b) \
    Testing::AssertGreaterThanImpl((a), (b), #a " > " #b, __FILE__, __LINE__)

/// Assert that a >= b.
#define TEST_ASSERT_GE(a, b) \
    Testing::AssertGreaterEqualImpl((a), (b), #a " >= " #b, __FILE__, __LINE__)

/// Assert that a pointer is nullptr.
#define TEST_ASSERT_NULL(ptr) \
    Testing::AssertNullImpl((ptr), #ptr " == nullptr", __FILE__, __LINE__)

/// Assert that a pointer is not nullptr.
#define TEST_ASSERT_NOT_NULL(ptr) \
    Testing::AssertNotNullImpl((ptr), #ptr " != nullptr", __FILE__, __LINE__)
