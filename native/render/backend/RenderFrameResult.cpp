#include "render/backend/RenderFrameResult.h"

#include <cstring>

namespace cellularity::render {
namespace {

constexpr std::uint64_t kOutcomeMask = 0xFFU;
constexpr std::uint64_t kFlagsMask = 0xFFU;
constexpr std::uint64_t kReservedMask = 0xFFFFU;
constexpr unsigned kFlagsShift = 8;
constexpr unsigned kReservedShift = 16;
constexpr unsigned kSemanticValueShift = 32;
constexpr std::uint32_t kKnownFlags =
        kPrimaryContentPresented | kFallbackClearPresented;

bool isKnownOutcome(RenderFrameOutcome outcome) noexcept {
    switch (outcome) {
        case RenderFrameOutcome::Presented:
        case RenderFrameOutcome::Deferred:
        case RenderFrameOutcome::SwapchainRecreated:
        case RenderFrameOutcome::SurfaceLost:
        case RenderFrameOutcome::SkippedNotReady:
        case RenderFrameOutcome::Failed:
            return true;
    }
    return false;
}

} // namespace

bool isValidRenderFrameResult(const RenderFrameResult& result) noexcept {
    if (!isKnownOutcome(result.outcome)
            || (result.flags & ~kKnownFlags) != 0) {
        return false;
    }
    if (result.outcome == RenderFrameOutcome::Presented) {
        return result.flags == kPrimaryContentPresented
                || result.flags == kFallbackClearPresented;
    }
    return result.flags == 0;
}

std::uint64_t packRenderFrameResult(const RenderFrameResult& result) noexcept {
    if (!isValidRenderFrameResult(result)) {
        return kInvalidRenderFrameWire;
    }
    const auto outcome = static_cast<std::uint32_t>(result.outcome);
    return static_cast<std::uint64_t>(outcome)
            | (static_cast<std::uint64_t>(result.flags) << kFlagsShift)
            | (static_cast<std::uint64_t>(
                       static_cast<std::uint32_t>(result.semanticValue))
                    << kSemanticValueShift);
}

std::int64_t packRenderFrameResultSigned(
        const RenderFrameResult& result) noexcept {
    const std::uint64_t bits = packRenderFrameResult(result);
    std::int64_t signedBits = 0;
    std::memcpy(&signedBits, &bits, sizeof(signedBits));
    return signedBits;
}

bool unpackRenderFrameResult(
        std::uint64_t bits, RenderFrameResult& result) noexcept {
    if (((bits >> kReservedShift) & kReservedMask) != 0) {
        return false;
    }
    const std::uint32_t semanticBits =
            static_cast<std::uint32_t>(bits >> kSemanticValueShift);
    std::int32_t semanticValue = 0;
    std::memcpy(&semanticValue, &semanticBits, sizeof(semanticValue));
    RenderFrameResult decoded{
            static_cast<RenderFrameOutcome>(bits & kOutcomeMask),
            static_cast<std::uint32_t>(
                    (bits >> kFlagsShift) & kFlagsMask),
            semanticValue,
    };
    if (!isValidRenderFrameResult(decoded)) {
        return false;
    }
    result = decoded;
    return true;
}

RenderFrameResult makeRenderFrameResult(RenderFrameOutcome outcome,
        std::uint32_t presentedFlag,
        std::int32_t semanticValue) noexcept {
    return {outcome,
            outcome == RenderFrameOutcome::Presented ? presentedFlag : 0,
            semanticValue};
}

RenderFrameResult withPresentedContent(RenderFrameResult result,
        std::uint32_t presentedFlag,
        std::int32_t semanticValue) noexcept {
    if (!isValidRenderFrameResult(result)) {
        return result;
    }
    result.flags = result.outcome == RenderFrameOutcome::Presented
            ? presentedFlag
            : 0;
    result.semanticValue = semanticValue;
    return result;
}

} // namespace cellularity::render
