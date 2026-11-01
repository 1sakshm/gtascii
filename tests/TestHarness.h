#pragma once

// Minimal self-registering test harness (no external dependencies).
//
//   TEST_CASE("name") { CHECK(expr); CHECK_EQ(a, b); }

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace avtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void reportFailure(const char* file, int line, const std::string& message)
{
    ++failureCount();
    std::printf("    FAILED %s:%d: %s\n", file, line, message.c_str());
}

template <typename T>
std::string describe(const T& value)
{
    std::ostringstream os;
    if constexpr (std::is_same_v<T, char32_t>) {
        os << "U+" << std::hex << static_cast<unsigned long>(value);
    } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) {
        os << static_cast<int>(value);
    } else {
        os << value;
    }
    return os.str();
}

} // namespace avtest

#define AVTEST_CONCAT_INNER(a, b) a##b
#define AVTEST_CONCAT(a, b) AVTEST_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                  \
    static void AVTEST_CONCAT(avtest_fn_, __LINE__)();                                   \
    static const ::avtest::Registrar AVTEST_CONCAT(avtest_reg_, __LINE__)(               \
        name, &AVTEST_CONCAT(avtest_fn_, __LINE__));                                     \
    static void AVTEST_CONCAT(avtest_fn_, __LINE__)()

#define CHECK(expr)                                                                      \
    do {                                                                                 \
        if (!(expr)) {                                                                   \
            ::avtest::reportFailure(__FILE__, __LINE__, "CHECK(" #expr ")");             \
        }                                                                                \
    } while (0)

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        const auto& avtest_a = (actual);                                                 \
        const auto& avtest_e = (expected);                                               \
        if (!(avtest_a == avtest_e)) {                                                   \
            ::avtest::reportFailure(__FILE__, __LINE__,                                  \
                                    "CHECK_EQ(" #actual ", " #expected "): got " +       \
                                        ::avtest::describe(avtest_a) + ", expected " +   \
                                        ::avtest::describe(avtest_e));                   \
        }                                                                                \
    } while (0)
