// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Minimal dependency-free test framework.  The project intentionally does not
// vendor a test library: keeping the test binary self-contained makes the
// hermetic CI job reproducible on machines without a package manager.

#pragma once

#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace rtx::test {

struct Context {
    std::vector<std::string> failures;
};

using TestFunction = void (*)(Context&);

struct TestCase {
    std::string name;
    TestFunction function = nullptr;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, TestFunction function) {
        registry().push_back(TestCase{name != nullptr ? name : "<unnamed>", function});
    }
};

inline void fail(Context& context, const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ':' << line << ": " << message;
    context.failures.push_back(out.str());
}

template <typename T>
std::string stringify(const T& value) {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_enum_v<Value>) {
        return "enum(" + std::to_string(static_cast<long long>(value)) + ")";
    } else if constexpr (std::is_pointer_v<Value>) {
        if (value == nullptr) {
            return "nullptr";
        }
        std::ostringstream out;
        out << static_cast<const void*>(value);
        return out.str();
    } else if constexpr (std::is_integral_v<Value> && sizeof(Value) == 1) {
        return std::to_string(static_cast<int>(value));
    } else if constexpr (requires(std::ostream& stream, const Value& item) {
                             stream << item;
                         }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<unprintable>";
    }
}

template <typename A, typename B>
void checkEqual(Context& context, const char* file, int line, const char* actual_text,
                const char* expected_text, const A& actual, const B& expected) {
    if (actual == expected) {
        return;
    }
    fail(context, file, line,
         std::string(actual_text) + " == " + expected_text + " (actual " + stringify(actual) +
             ", expected " + stringify(expected) + ")");
}

inline void checkNear(Context& context, const char* file, int line, const char* actual_text,
                      const char* expected_text, double actual, double expected, double tolerance) {
    if (std::isfinite(actual) && std::isfinite(expected) &&
        std::fabs(actual - expected) <= tolerance) {
        return;
    }
    std::ostringstream message;
    message.precision(17);
    message << actual_text << " ~= " << expected_text << " (actual " << actual << ", expected "
            << expected << ", tolerance " << tolerance << ")";
    fail(context, file, line, message.str());
}

}  // namespace rtx::test

#define RTX_TEST(name)                                                              \
    static void name(::rtx::test::Context& rtx_test_context);                       \
    static ::rtx::test::Registrar name##_registrar(#name, &name);                   \
    static void name(::rtx::test::Context& rtx_test_context)

#define RTX_CHECK(expression)                                                       \
    do {                                                                            \
        if (!(expression)) {                                                        \
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,                 \
                              "CHECK failed: " #expression);                        \
        }                                                                           \
    } while (false)

#define RTX_REQUIRE(expression)                                                     \
    do {                                                                            \
        if (!(expression)) {                                                        \
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,                 \
                              "REQUIRE failed: " #expression);                      \
            return;                                                                 \
        }                                                                           \
    } while (false)

#define RTX_CHECK_EQ(actual, expected)                                              \
    do {                                                                            \
        const auto& rtx_actual = (actual);                                          \
        const auto& rtx_expected = (expected);                                      \
        ::rtx::test::checkEqual(rtx_test_context, __FILE__, __LINE__, #actual,      \
                                #expected, rtx_actual, rtx_expected);               \
    } while (false)

#define RTX_REQUIRE_EQ(actual, expected)                                            \
    do {                                                                            \
        const auto& rtx_actual = (actual);                                          \
        const auto& rtx_expected = (expected);                                      \
        if (!(rtx_actual == rtx_expected)) {                                        \
            ::rtx::test::checkEqual(rtx_test_context, __FILE__, __LINE__, #actual,  \
                                    #expected, rtx_actual, rtx_expected);           \
            return;                                                                 \
        }                                                                           \
    } while (false)

#define RTX_CHECK_NE(actual, expected)                                              \
    do {                                                                            \
        const auto& rtx_actual = (actual);                                          \
        const auto& rtx_expected = (expected);                                      \
        if (!(rtx_actual != rtx_expected)) {                                        \
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,                 \
                              "CHECK_NE failed: " #actual " != " #expected);        \
        }                                                                           \
    } while (false)

#define RTX_CHECK_NEAR(actual, expected, tolerance)                                 \
    do {                                                                            \
        const double rtx_actual = static_cast<double>(actual);                      \
        const double rtx_expected = static_cast<double>(expected);                  \
        const double rtx_tolerance = static_cast<double>(tolerance);                \
        ::rtx::test::checkNear(rtx_test_context, __FILE__, __LINE__, #actual,       \
                               #expected, rtx_actual, rtx_expected, rtx_tolerance); \
    } while (false)

#define RTX_CHECK_THROWS(expression)                                                \
    do {                                                                            \
        bool rtx_test_threw = false;                                                \
        try {                                                                       \
            expression;                                                             \
        } catch (...) {                                                             \
            rtx_test_threw = true;                                                  \
        }                                                                           \
        if (!rtx_test_threw) {                                                      \
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,                 \
                              "expected exception: " #expression);                  \
        }                                                                           \
    } while (false)
