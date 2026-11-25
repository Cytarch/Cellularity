#pragma once

// Exact float arithmetic carried out in normal doubles.
//
// On x86 every float operation with a subnormal input or result takes a microcode assist
// that costs on the order of a hundred cycles. Quantities that decay towards zero, such as
// a settling food's velocity or a fading signal, spend long stretches in that range. These
// helpers produce the same bits as the float operations while every hardware operation
// stays in the normal double range:
//
// - A float + - * of two floats computed in double and then rounded once to float is the
//   correctly rounded float result, because 53 >= 2 * 24 + 2.
// - The rounding onto the float grid is done by hand below 2^-126, where the float grid is
//   the multiples of 2^-149.
//
// All of it assumes round-to-nearest, which the simulation's floating-point environment
// guarantees.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace cellularity::float_grid {

// The exact value of a float, without converting a subnormal float in hardware.
inline double exactFloatValue(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000U) != 0U) {
        return static_cast<double>(value);
    }
    const double magnitude = static_cast<double>(bits & 0x007fffffU) * 0x1p-149;
    return (bits & 0x80000000U) != 0U ? -magnitude : magnitude;
}

// x rounded to the nearest float (ties to even), returned as its exact double value. The
// sign of a result that rounds to zero is kept, as float rounding keeps it.
inline double roundToFloatGrid(double x) noexcept {
    const double magnitude = std::fabs(x);
    if (magnitude >= 0x1p-126) {
        return static_cast<double>(static_cast<float>(x));
    }
    // Adding 2^-97 moves the magnitude into [2^-97, 2^-96), whose doubles are exactly the
    // multiples of 2^-149, so the addition rounds to the float grid with ties to even, and
    // the subtraction is exact.
    return std::copysign((magnitude + 0x1p-97) - 0x1p-97, x);
}

// The float holding a value already on the float grid, built from bits when subnormal.
inline float floatFromGridValue(double value) noexcept {
    if (std::fabs(value) >= 0x1p-126) {
        return static_cast<float>(value);
    }
    std::uint32_t bits = static_cast<std::uint32_t>(std::fabs(value) * 0x1p149);
    if (std::signbit(value)) {
        bits |= 0x80000000U;
    }
    float result = 0.0F;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

}  // namespace cellularity::float_grid
