#include "app/EnvironmentFieldPreviewState.h"

#include <cmath>
#include <cstring>

#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "core/Environment.h"

namespace cellularity::app {
namespace {

constexpr double kPreviewDishRadius = 1.0;
constexpr double kDensityDisplayMinimum = -3.3;
constexpr double kDensityDisplayMaximum = 3.3;
constexpr double kUnitDisplayMinimum = 0.0;
constexpr double kUnitDisplayMaximum = 1.0;

bool isScalarTarget(std::int32_t target) noexcept {
    switch (target) {
        case kEnvironmentFieldTargetDensity:
        case kEnvironmentFieldTargetSalinity:
        case kEnvironmentFieldTargetFoodLipidLayer:
            return true;
        case kEnvironmentFieldTargetGravity:
            return false;
        default:
            return false;
    }
}

bool isKnownScalarFieldType(std::int32_t fieldType) noexcept {
    switch (fieldType) {
        case kEnvironmentScalarFieldConstant:
        case kEnvironmentScalarFieldLinearDirectional:
        case kEnvironmentScalarFieldRadial:
            return true;
        default:
            return false;
    }
}

bool isKnownVectorFieldType(std::int32_t fieldType) noexcept {
    switch (fieldType) {
        case kEnvironmentVectorFieldLinearDirectional:
        case kEnvironmentVectorFieldRadialInward:
        case kEnvironmentVectorFieldRadialOutward:
            return true;
        default:
            return false;
    }
}

bool isCanonicalPositiveZero(double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits == 0;
}

std::uint64_t doubleBits(double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void writeLittleEndian16(std::uint8_t* destination,
        std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void writeLittleEndian32(std::uint8_t* destination,
        std::uint32_t value) noexcept {
    for (unsigned index = 0; index < 4; ++index) {
        destination[index] =
                static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void writeLittleEndian64(std::uint8_t* destination,
        std::uint64_t value) noexcept {
    for (unsigned index = 0; index < 8; ++index) {
        destination[index] =
                static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

int mapScalarFieldType(std::int32_t fieldType) noexcept {
    switch (fieldType) {
        case kEnvironmentScalarFieldConstant:
            return Environment::DENSITY_FIELD_CONSTANT;
        case kEnvironmentScalarFieldLinearDirectional:
            return Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL;
        case kEnvironmentScalarFieldRadial:
            return Environment::DENSITY_FIELD_RADIAL;
        default:
            return Environment::DENSITY_FIELD_CONSTANT;
    }
}

int mapVectorFieldType(std::int32_t fieldType) noexcept {
    switch (fieldType) {
        case kEnvironmentVectorFieldLinearDirectional:
            return Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL;
        case kEnvironmentVectorFieldRadialInward:
            return Environment::VECTOR_FIELD_RADIAL_INWARD;
        case kEnvironmentVectorFieldRadialOutward:
            return Environment::VECTOR_FIELD_RADIAL_OUTWARD;
        default:
            return Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL;
    }
}

} // namespace

EnvironmentFieldPreviewValidationError validateEnvironmentFieldPreviewUpdate(
        const EnvironmentFieldPreviewUpdate& update) noexcept {
    const bool targetIsScalar = isScalarTarget(update.target);
    if (!targetIsScalar && update.target != kEnvironmentFieldTargetGravity) {
        return EnvironmentFieldPreviewValidationError::UnknownTarget;
    }
    if (targetIsScalar ? !isKnownScalarFieldType(update.fieldType)
                       : !isKnownVectorFieldType(update.fieldType)) {
        return EnvironmentFieldPreviewValidationError::UnknownFieldType;
    }
    if (!std::isfinite(update.base) || !std::isfinite(update.strength)
            || !std::isfinite(update.angle) || !std::isfinite(update.offset)
            || !std::isfinite(update.softness)) {
        return EnvironmentFieldPreviewValidationError::NonFiniteValue;
    }
    if (update.target == kEnvironmentFieldTargetGravity
            && (!isCanonicalPositiveZero(update.base)
                    || !isCanonicalPositiveZero(update.offset)
                    || !isCanonicalPositiveZero(update.softness))) {
        return EnvironmentFieldPreviewValidationError::NonCanonicalGravityValue;
    }
    return EnvironmentFieldPreviewValidationError::None;
}

bool environmentFieldPreviewUpdatesEqual(
        const EnvironmentFieldPreviewUpdate& left,
        const EnvironmentFieldPreviewUpdate& right) noexcept {
    return left.target == right.target && left.fieldType == right.fieldType
            && doubleBits(left.base) == doubleBits(right.base)
            && doubleBits(left.strength) == doubleBits(right.strength)
            && doubleBits(left.angle) == doubleBits(right.angle)
            && doubleBits(left.offset) == doubleBits(right.offset)
            && doubleBits(left.softness) == doubleBits(right.softness);
}

std::optional<render::EnvironmentFieldPreviewCommand>
EnvironmentFieldPreviewState::toRenderCommand(
        const NativeViewport& viewport) const noexcept {
    if (ingressRevision == 0 || !viewport.isReady()
            || viewport.viewWidth() == 0.0f
            || viewport.viewHeight() == 0.0f
            || validateEnvironmentFieldPreviewUpdate(value)
                    != EnvironmentFieldPreviewValidationError::None) {
        return std::nullopt;
    }

    render::EnvironmentFieldPreviewCommand command;
    command.matrix = viewport.worldMatrix(identityMatrix());
    command.surfaceWidth = viewport.surfaceWidth();
    command.surfaceHeight = viewport.surfaceHeight();
    command.dishRadius = kPreviewDishRadius;
    command.fieldBase = value.base;
    command.fieldStrength = value.strength;
    command.fieldAngle = value.angle;
    command.fieldOffset = value.offset;
    command.fieldSoftness = value.softness;

    switch (value.target) {
        case kEnvironmentFieldTargetDensity:
            command.fieldType = mapScalarFieldType(value.fieldType);
            command.displayMin = kDensityDisplayMinimum;
            command.displayMax = kDensityDisplayMaximum;
            break;
        case kEnvironmentFieldTargetSalinity:
        case kEnvironmentFieldTargetFoodLipidLayer:
            command.fieldType = mapScalarFieldType(value.fieldType);
            command.displayMin = kUnitDisplayMinimum;
            command.displayMax = kUnitDisplayMaximum;
            break;
        case kEnvironmentFieldTargetGravity:
            command.vectorPreview = true;
            command.gravityFieldType = mapVectorFieldType(value.fieldType);
            command.gravityFieldAngle = value.angle;
            break;
        default:
            return std::nullopt;
    }
    return command;
}

std::array<std::uint8_t, kEnvironmentFieldPreviewResultSize>
encodeEnvironmentFieldPreviewUpdateResult(
        EnvironmentFieldPreviewUpdateResult result) noexcept {
    std::array<std::uint8_t, kEnvironmentFieldPreviewResultSize> bytes{};
    writeLittleEndian32(bytes.data(), kEnvironmentFieldPreviewResultMagic);
    writeLittleEndian16(bytes.data() + 4, kEnvironmentFieldPreviewResultVersion);
    writeLittleEndian16(bytes.data() + 6, kEnvironmentFieldPreviewResultSize);
    writeLittleEndian32(
            bytes.data() + 8, static_cast<std::uint32_t>(result.status));
    writeLittleEndian64(bytes.data() + 16, result.ingressRevision);
    return bytes;
}

} // namespace cellularity::app
