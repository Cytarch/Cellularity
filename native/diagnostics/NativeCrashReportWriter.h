#pragma once

#include <cstddef>
#include <cstdint>

namespace cellularity::diagnostics {

inline constexpr std::size_t NATIVE_REPORT_MAX_BYTES = 4096;
inline constexpr const char* NATIVE_REPORT_FILE_NAME = "native-crash.txt";

struct NativeCrashReportRecord {
    std::int64_t epochSeconds = 0;
    const char* appVersion = "unknown";
    const char* device = "unknown";
    const char* supportedAbis = "unknown";
    const char* renderBackend = "unknown";
    const char* renderBackendFallbackReason = "none";
    int signalNumber = 0;
    const char* signalName = "UNKNOWN";
    int signalCode = 0;
    long processId = 0;
    long threadId = 0;
    std::uintptr_t faultAddress = 0;
    std::uintptr_t programCounter = 0;
};

std::size_t formatNativeCrashReport(
        char* destination,
        std::size_t destinationSize,
        const NativeCrashReportRecord& record) noexcept;

bool writeNativeCrashReport(
        const char* directory,
        const NativeCrashReportRecord& record) noexcept;

} // namespace cellularity::diagnostics
