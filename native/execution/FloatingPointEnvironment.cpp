#include "execution/FloatingPointEnvironment.h"

#include <cfenv>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace cellularity::execution {
namespace {

#if defined(__SSE2__) || defined(_M_X64)
constexpr std::uint32_t kArchitectureControlMask =
        (3U << 13U) | // MXCSR rounding control
        (1U << 15U) | // flush-to-zero
        (1U << 6U); // denormals-are-zero

std::uint64_t readArchitectureControl() noexcept {
    return static_cast<std::uint64_t>(_mm_getcsr() & kArchitectureControlMask);
}

void writeArchitectureControl(std::uint64_t policy) noexcept {
    const std::uint32_t current = _mm_getcsr();
    const std::uint32_t replacement =
            (current & ~kArchitectureControlMask) | (static_cast<std::uint32_t>(policy) & kArchitectureControlMask);
    _mm_setcsr(replacement);
}

#elif defined(__aarch64__)
constexpr std::uint64_t kArchitectureControlMask =
        (1ULL << 19U) | // FZ16
        (3ULL << 22U) | // rounding mode
        (1ULL << 24U) | // flush-to-zero
        (1ULL << 25U) | // default NaN
        (1ULL << 26U); // alternative half precision

std::uint64_t readRawArchitectureControl() noexcept {
    std::uint64_t value = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}

std::uint64_t readArchitectureControl() noexcept {
    return readRawArchitectureControl() & kArchitectureControlMask;
}

void writeArchitectureControl(std::uint64_t policy) noexcept {
    const std::uint64_t replacement =
            (readRawArchitectureControl() & ~kArchitectureControlMask) | (policy & kArchitectureControlMask);
    __asm__ volatile("msr fpcr, %0" : : "r"(replacement));
}

#elif defined(__arm__) && defined(__VFP_FP__)
constexpr std::uint32_t kArchitectureControlMask =
        (3U << 22U) | // rounding mode
        (1U << 24U) | // flush-to-zero
        (1U << 25U) | // default NaN
        (1U << 26U); // alternative half precision

std::uint32_t readRawArchitectureControl() noexcept {
    std::uint32_t value = 0;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(value));
    return value;
}

std::uint64_t readArchitectureControl() noexcept {
    return readRawArchitectureControl() & kArchitectureControlMask;
}

void writeArchitectureControl(std::uint64_t policy) noexcept {
    const std::uint32_t replacement =
            (readRawArchitectureControl() & ~kArchitectureControlMask) | (static_cast<std::uint32_t>(policy) & kArchitectureControlMask);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(replacement));
}
#endif

constexpr bool hasArchitectureControl() noexcept {
#if defined(__SSE2__) || defined(_M_X64) || defined(__aarch64__) || (defined(__arm__) && defined(__VFP_FP__))
    return true;
#else
    return false;
#endif
}

} // namespace

FloatingPointEnvironment captureFloatingPointEnvironment() noexcept {
    FloatingPointEnvironment result;
    result.roundingMode = std::fegetround();
    result.hasArchitectureControl = hasArchitectureControl();
#if defined(__SSE2__) || defined(_M_X64) || defined(__aarch64__) || (defined(__arm__) && defined(__VFP_FP__))
    result.architectureControl = readArchitectureControl();
#endif
    result.valid = result.roundingMode != -1;
    return result;
}

bool floatingPointEnvironmentMatches(
        const FloatingPointEnvironment& expected) noexcept {
    if (!expected.valid || std::fegetround() != expected.roundingMode) {
        return false;
    }
    if (!expected.hasArchitectureControl) {
        return true;
    }
#if defined(__SSE2__) || defined(_M_X64) || defined(__aarch64__) || (defined(__arm__) && defined(__VFP_FP__))
    return readArchitectureControl() == expected.architectureControl;
#else
    return false;
#endif
}

bool applyFloatingPointEnvironment(
        const FloatingPointEnvironment& environment) noexcept {
    if (!environment.valid || std::fesetround(environment.roundingMode) != 0) {
        return false;
    }
    if (environment.hasArchitectureControl) {
#if defined(__SSE2__) || defined(_M_X64) || defined(__aarch64__) || (defined(__arm__) && defined(__VFP_FP__))
        writeArchitectureControl(environment.architectureControl);
#else
        return false;
#endif
    }
    return floatingPointEnvironmentMatches(environment);
}

} // namespace cellularity::execution
