#pragma once

#include <cstdint>

#if defined(__ANDROID__)
#include <cerrno>
#include <sys/resource.h>
#endif

namespace cellularity::platform {

enum class CurrentThreadSchedulingNormalizationStatus : std::uint8_t {
    NotRun = 0,
    NotNeeded = 1,
    Applied = 2,
    NotApplicable = 3,
    ReadFailed = 4,
    WriteFailed = 5,
    VerificationFailed = 6,
};

struct CurrentThreadSchedulingNormalizationResult final {
    CurrentThreadSchedulingNormalizationStatus status =
            CurrentThreadSchedulingNormalizationStatus::NotRun;
    int inheritedNice = 0;
    int effectiveNice = 0;
    int errorNumber = 0;
    bool inheritedNiceKnown = false;
    bool effectiveNiceKnown = false;
};

// A negative nice value is an inherited priority boost. Neutralize only that
// boost: a non-negative value may be an intentional external restriction and
// must never be raised by the worker itself.
constexpr int neutralNiceForInheritedBoost(int inheritedNice) noexcept {
    return inheritedNice < 0 ? 0 : inheritedNice;
}

struct CurrentThreadNiceOperations final {
    void* context = nullptr;
    bool (*read)(void* context, int& value, int& errorNumber) noexcept =
            nullptr;
    bool (*write)(void* context, int value, int& errorNumber) noexcept =
            nullptr;
};

inline CurrentThreadSchedulingNormalizationResult
normalizeInheritedThreadPriorityBoost(
        const CurrentThreadNiceOperations& operations) noexcept {
    CurrentThreadSchedulingNormalizationResult result;
    if (operations.read == nullptr) {
        result.status =
                CurrentThreadSchedulingNormalizationStatus::ReadFailed;
        return result;
    }

    int inheritedNice = 0;
    int errorNumber = 0;
    if (!operations.read(
                operations.context, inheritedNice, errorNumber)) {
        result.status =
                CurrentThreadSchedulingNormalizationStatus::ReadFailed;
        result.errorNumber = errorNumber;
        return result;
    }
    result.inheritedNice = inheritedNice;
    result.inheritedNiceKnown = true;

    const int targetNice = neutralNiceForInheritedBoost(inheritedNice);
    if (targetNice == inheritedNice) {
        result.status =
                CurrentThreadSchedulingNormalizationStatus::NotNeeded;
        result.effectiveNice = inheritedNice;
        result.effectiveNiceKnown = true;
        return result;
    }

    if (operations.write == nullptr
            || !operations.write(
                    operations.context, targetNice, errorNumber)) {
        result.status =
                CurrentThreadSchedulingNormalizationStatus::WriteFailed;
        result.errorNumber = errorNumber;

        // A failed write is exceptional, so one extra read is acceptable and
        // gives diagnostics the actual effective value when it is available.
        int effectiveNice = 0;
        int ignoredError = 0;
        if (operations.read(operations.context, effectiveNice,
                    ignoredError)) {
            result.effectiveNice = effectiveNice;
            result.effectiveNiceKnown = true;
        }
        return result;
    }

    int effectiveNice = 0;
    errorNumber = 0;
    if (!operations.read(
                operations.context, effectiveNice, errorNumber)) {
        result.status = CurrentThreadSchedulingNormalizationStatus::
                VerificationFailed;
        result.errorNumber = errorNumber;
        return result;
    }
    result.effectiveNice = effectiveNice;
    result.effectiveNiceKnown = true;
    if (effectiveNice != targetNice) {
        result.status = CurrentThreadSchedulingNormalizationStatus::
                VerificationFailed;
        return result;
    }

    result.status = CurrentThreadSchedulingNormalizationStatus::Applied;
    return result;
}

inline const char* currentThreadSchedulingNormalizationStatusName(
        CurrentThreadSchedulingNormalizationStatus status) noexcept {
    switch (status) {
        case CurrentThreadSchedulingNormalizationStatus::NotRun:
            return "not_run";
        case CurrentThreadSchedulingNormalizationStatus::NotNeeded:
            return "not_needed";
        case CurrentThreadSchedulingNormalizationStatus::Applied:
            return "applied";
        case CurrentThreadSchedulingNormalizationStatus::NotApplicable:
            return "not_applicable";
        case CurrentThreadSchedulingNormalizationStatus::ReadFailed:
            return "read_failed";
        case CurrentThreadSchedulingNormalizationStatus::WriteFailed:
            return "write_failed";
        case CurrentThreadSchedulingNormalizationStatus::VerificationFailed:
            return "verification_failed";
    }
    return "unknown";
}

#if defined(__ANDROID__)
namespace detail {

inline bool readCurrentThreadNice(
        void*, int& value, int& errorNumber) noexcept {
    errno = 0;
    const int current = getpriority(PRIO_PROCESS, 0);
    if (current == -1 && errno != 0) {
        errorNumber = errno;
        return false;
    }
    value = current;
    errorNumber = 0;
    return true;
}

inline bool writeCurrentThreadNice(
        void*, int value, int& errorNumber) noexcept {
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, value) != 0) {
        errorNumber = errno;
        return false;
    }
    errorNumber = 0;
    return true;
}

} // namespace detail
#endif

inline CurrentThreadSchedulingNormalizationResult
normalizeCurrentThreadInheritedPriorityBoost() noexcept {
#if defined(__ANDROID__)
    const CurrentThreadNiceOperations operations{
            nullptr,
            detail::readCurrentThreadNice,
            detail::writeCurrentThreadNice,
    };
    return normalizeInheritedThreadPriorityBoost(operations);
#else
    CurrentThreadSchedulingNormalizationResult result;
    result.status =
            CurrentThreadSchedulingNormalizationStatus::NotApplicable;
    return result;
#endif
}

} // namespace cellularity::platform
