#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cellularity::app::preview {

inline constexpr std::uint32_t kPreviewCaptureAdmissionMagic = 0x31414350U;
inline constexpr std::uint16_t kPreviewCaptureAdmissionVersion = 1;
inline constexpr std::uint16_t kPreviewCaptureAdmissionSize = 24;

inline constexpr std::uint32_t kPreviewCaptureResultMagic = 0x31524350U;
inline constexpr std::uint16_t kPreviewCaptureResultVersion = 1;
inline constexpr std::uint16_t kPreviewCaptureResultSize = 56;

enum class PreviewCaptureAdmissionStatus : std::uint32_t {
    Accepted = 0,
    Busy = 1,
    InvalidArgument = 2,
    Closing = 3,
    BudgetExceeded = 4,
    DuplicateRequestId = 5,
    InternalFailure = 6,
};

enum class PreviewCaptureStatus : std::uint32_t {
    Completed = 0,
    Failed = 1,
    Cancelled = 2,
    TimedOut = 3,
    AlreadyConsumed = 4,
    InternalFailure = 5,
};

enum class PreviewCaptureCancelResult : std::int32_t {
    CancellationRequested = 0,
    AlreadyTerminal = 1,
    AlreadyCancelled = 2,
};

enum class PreviewCapturePixelFormat : std::uint32_t {
    None = 0,
    Rgba8 = 1,
};

enum class PreviewCaptureOrigin : std::uint32_t {
    None = 0,
    TopLeft = 1,
};

enum class PreviewCaptureAlphaMode : std::uint32_t {
    None = 0,
    Straight = 1,
    Opaque = 2,
};

struct PreviewCaptureAdmission final {
    PreviewCaptureAdmissionStatus status = PreviewCaptureAdmissionStatus::InvalidArgument;
    std::uint64_t requestHandle = 0;
};

struct PreviewCaptureResultHeader final {
    std::uint64_t requestId = 0;
    PreviewCaptureStatus status = PreviewCaptureStatus::InternalFailure;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowStrideBytes = 0;
    PreviewCapturePixelFormat pixelFormat = PreviewCapturePixelFormat::None;
    PreviewCaptureOrigin origin = PreviewCaptureOrigin::None;
    PreviewCaptureAlphaMode alphaMode = PreviewCaptureAlphaMode::None;
    std::uint64_t payloadBytes = 0;
};

[[nodiscard]] std::array<std::uint8_t, kPreviewCaptureAdmissionSize> encodePreviewCaptureAdmission(PreviewCaptureAdmission value) noexcept;

[[nodiscard]] std::array<std::uint8_t, kPreviewCaptureResultSize> encodePreviewCaptureResultHeader(const PreviewCaptureResultHeader& value) noexcept;

[[nodiscard]] bool validateCompletedPreviewCaptureResult(const PreviewCaptureResultHeader& value, std::size_t payloadSize) noexcept;

} // namespace cellularity::app::preview
