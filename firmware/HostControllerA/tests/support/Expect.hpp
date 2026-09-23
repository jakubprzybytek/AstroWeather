#pragma once

// Minimal assertion helpers shared by the native test suites. A failed check
// prints the case name (and both values for expectEqual) and is counted;
// finish() turns the count into the process exit code that CTest reads.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace Test {

inline int& failureCount()
{
    static int failures = 0;
    return failures;
}

inline void fail(const char* caseName)
{
    std::cerr << caseName << " failed\n";
    ++failureCount();
}

inline void expect(bool condition, const char* caseName)
{
    if (!condition) {
        fail(caseName);
    }
}

// Prints small integer types as numbers rather than characters.
template <typename T>
auto printable(const T& value)
{
    if constexpr (std::is_enum_v<T>) {
        return static_cast<long long>(value);
    } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1U) {
        return static_cast<int>(value);
    } else {
        return value;
    }
}

template <typename Actual, typename Expected>
void expectEqual(const Actual& actual, const Expected& expected, const char* caseName)
{
    if (!(actual == expected)) {
        std::cerr << caseName << " failed: expected " << printable(expected) << ", got "
                  << printable(actual) << '\n';
        ++failureCount();
    }
}

inline int finish(const char* suiteName)
{
    if (failureCount() != 0) {
        std::cerr << failureCount() << ' ' << suiteName << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace Test
