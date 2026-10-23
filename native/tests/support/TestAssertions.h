#pragma once

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace cellularity::tests {

inline void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

inline void assertTrue(bool value, const std::string& message) {
    if (!value) {
        fail(message);
    }
}

inline void assertNear(double expected, double actual, double tolerance, const std::string& message) {
    if (std::fabs(expected - actual) > tolerance) {
        fail(message + ": expected=" + std::to_string(expected) + ", actual=" + std::to_string(actual));
    }
}

template <typename Action>
inline void assertThrows(Action action, const std::string& message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    fail(message);
}

} // namespace cellularity::tests
