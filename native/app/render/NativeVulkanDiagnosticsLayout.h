#pragma once

#include <cstddef>
#include <cstdint>

namespace cellularity::app::render_worker {

inline constexpr std::uint32_t kNativeVulkanDiagnosticsMagic = 0x31434456U;
inline constexpr std::uint16_t kNativeVulkanDiagnosticsVersion = 1;
inline constexpr std::size_t kNativeVulkanDiagnosticsHeaderSize = 32;
inline constexpr std::size_t kNativeVulkanDiagnosticsMaximumChunkSize =
        64U * 1024U;

enum class DiagnosticsRequestAdmission : std::uint8_t {
    Accepted = 0,
    Busy = 1,
    InvalidRequestId = 2,
    Closing = 3,
    RevisionExhausted = 4,
};

enum class DiagnosticsChunkReadStatus : std::uint8_t {
    Ready = 0,
    Pending = 1,
    StaleRevision = 2,
    InvalidOffset = 3,
    Cancelled = 4,
    Failed = 5,
    InvalidRequestId = 6,
    InvalidBuffer = 7,
};

enum class DiagnosticsReleaseResult : std::uint8_t {
    Released = 0,
    CancellationRequested = 1,
    StaleRevision = 2,
    InvalidRequestId = 3,
    AlreadyReleased = 4,
};

struct DiagnosticsChunkResult final {
    DiagnosticsChunkReadStatus status = DiagnosticsChunkReadStatus::Failed;
    std::uint64_t revision = 0;
    std::uint64_t totalBytes = 0;
    std::uint32_t bytesWritten = 0;
};

} // namespace cellularity::app::render_worker
