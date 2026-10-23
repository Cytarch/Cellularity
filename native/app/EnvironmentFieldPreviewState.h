#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "render/backend/RenderSurfaceSession.h"

class NativeViewport;

namespace cellularity::app {

inline constexpr std::int32_t kEnvironmentFieldTargetDensity = 0;
inline constexpr std::int32_t kEnvironmentFieldTargetSalinity = 1;
inline constexpr std::int32_t kEnvironmentFieldTargetFoodLipidLayer = 2;
inline constexpr std::int32_t kEnvironmentFieldTargetGravity = 3;

inline constexpr std::int32_t kEnvironmentScalarFieldConstant = 0;
inline constexpr std::int32_t kEnvironmentScalarFieldLinearDirectional = 1;
inline constexpr std::int32_t kEnvironmentScalarFieldRadial = 2;

inline constexpr std::int32_t kEnvironmentVectorFieldLinearDirectional = 0;
inline constexpr std::int32_t kEnvironmentVectorFieldRadialInward = 1;
inline constexpr std::int32_t kEnvironmentVectorFieldRadialOutward = 2;

inline constexpr std::uint32_t kEnvironmentFieldPreviewResultMagic =
        0x31414645U;
inline constexpr std::uint16_t kEnvironmentFieldPreviewResultVersion = 1;
inline constexpr std::uint16_t kEnvironmentFieldPreviewResultSize = 24;

enum class EnvironmentFieldPreviewUpdateStatus : std::uint32_t {
    Applied = 0,
    Duplicate = 1,
    StaleEndpoint = 2,
    Closing = 3,
    Malformed = 4,
    RevisionExhausted = 5,
    WrongEndpointKind = 6,
};

enum class EnvironmentFieldPreviewValidationError : std::uint8_t {
    None = 0,
    UnknownTarget = 1,
    UnknownFieldType = 2,
    NonFiniteValue = 3,
    NonCanonicalGravityValue = 4,
};

struct EnvironmentFieldPreviewUpdate final {
    std::int32_t target = kEnvironmentFieldTargetDensity;
    std::int32_t fieldType = kEnvironmentScalarFieldConstant;
    double base = 0.0;
    double strength = 0.0;
    double angle = 0.0;
    double offset = 0.0;
    double softness = 0.0;
};

struct EnvironmentFieldPreviewState final {
    std::uint64_t ingressRevision = 0;
    EnvironmentFieldPreviewUpdate value{};

    [[nodiscard]] std::optional<render::EnvironmentFieldPreviewCommand>
    toRenderCommand(const NativeViewport& viewport) const noexcept;
};

struct EnvironmentFieldPreviewUpdateResult final {
    EnvironmentFieldPreviewUpdateStatus status =
            EnvironmentFieldPreviewUpdateStatus::Malformed;
    std::uint64_t ingressRevision = 0;
};

[[nodiscard]] EnvironmentFieldPreviewValidationError
validateEnvironmentFieldPreviewUpdate(
        const EnvironmentFieldPreviewUpdate& update) noexcept;

[[nodiscard]] bool environmentFieldPreviewUpdatesEqual(
        const EnvironmentFieldPreviewUpdate& left,
        const EnvironmentFieldPreviewUpdate& right) noexcept;

[[nodiscard]] std::array<std::uint8_t,
        kEnvironmentFieldPreviewResultSize>
encodeEnvironmentFieldPreviewUpdateResult(
        EnvironmentFieldPreviewUpdateResult result) noexcept;

} // namespace cellularity::app
