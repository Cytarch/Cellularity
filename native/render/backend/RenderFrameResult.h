#pragma once

#include <cstdint>

namespace cellularity::render {

enum class RenderFrameOutcome : std::int32_t {
    Presented = 0,
    Deferred = 1,
    SwapchainRecreated = 2,
    SurfaceLost = 3,
    SkippedNotReady = 4,
    Failed = 5,
};

enum RenderFrameFlags : std::uint32_t {
    kPrimaryContentPresented = 1U << 0,
    kFallbackClearPresented = 1U << 1,
};

enum class RenderPassStatus : std::uint8_t {
    Completed = 0,
    Failed = 1,
};

struct RenderFrameResult final {
    RenderFrameOutcome outcome = RenderFrameOutcome::SkippedNotReady;
    std::uint32_t flags = 0;
    std::int32_t semanticValue = 0;
};

inline constexpr std::uint64_t kInvalidRenderFrameWire = 0xFFU;

[[nodiscard]] bool isValidRenderFrameResult(
        const RenderFrameResult& result) noexcept;

[[nodiscard]] std::uint64_t packRenderFrameResult(
        const RenderFrameResult& result) noexcept;

[[nodiscard]] std::int64_t packRenderFrameResultSigned(
        const RenderFrameResult& result) noexcept;

[[nodiscard]] bool unpackRenderFrameResult(
        std::uint64_t bits, RenderFrameResult& result) noexcept;

[[nodiscard]] RenderFrameResult makeRenderFrameResult(
        RenderFrameOutcome outcome,
        std::uint32_t presentedFlag = 0,
        std::int32_t semanticValue = 0) noexcept;

[[nodiscard]] RenderFrameResult withPresentedContent(
        RenderFrameResult result,
        std::uint32_t presentedFlag,
        std::int32_t semanticValue = 0) noexcept;

[[nodiscard]] constexpr bool isPrimaryContentPresented(
        const RenderFrameResult& result) noexcept {
    return result.outcome == RenderFrameOutcome::Presented
            && result.flags == kPrimaryContentPresented;
}

static_assert(static_cast<std::int32_t>(RenderFrameOutcome::Presented) == 0);
static_assert(static_cast<std::int32_t>(RenderFrameOutcome::Deferred) == 1);
static_assert(static_cast<std::int32_t>(RenderFrameOutcome::SwapchainRecreated)
        == 2);
static_assert(static_cast<std::int32_t>(RenderFrameOutcome::SurfaceLost) == 3);
static_assert(static_cast<std::int32_t>(RenderFrameOutcome::SkippedNotReady)
        == 4);
static_assert(static_cast<std::int32_t>(RenderFrameOutcome::Failed) == 5);
static_assert(static_cast<std::uint32_t>(RenderFrameOutcome::Failed) <= 0xFFU);
static_assert((kPrimaryContentPresented | kFallbackClearPresented) <= 0xFFU);

} // namespace cellularity::render
