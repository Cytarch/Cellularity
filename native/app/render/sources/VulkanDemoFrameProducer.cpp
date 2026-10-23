#include "app/render/sources/VulkanDemoFrameProducer.h"

#include <cstddef>
#include <utility>

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"
#include "render/backend/vulkan/VulkanBasicCellFrame.h"
#include "render/frame/PreparedRenderFrame.h"

namespace cellularity::app::render_worker::sources {

VulkanDemoFrameProducer::VulkanDemoFrameProducer() = default;
VulkanDemoFrameProducer::~VulkanDemoFrameProducer() = default;

void VulkanDemoFrameProducer::setPerformanceHudEnabled(bool enabled) noexcept {
    mPerformanceHudEnabled = enabled;
}

SourceSurfaceResult VulkanDemoFrameProducer::bindBackend(
        std::shared_ptr<cellularity::render::abi::VulkanBackendSession>
                session) noexcept {
    try {
        if (session == nullptr) {
            return SourceSurfaceResult::Failed;
        }
        mAdapter = std::make_unique<cellularity::render::abi::
                        VulkanRenderSurfaceSessionAdapter>(std::move(session));
        mFrame = std::make_unique<cellularity::render::PreparedRenderFrame>(
                cellularity::render::vulkan::makeVulkanBasicCellDemoFrame());
        mTelemetry = std::make_unique<cellularity::microscope::
                        MicroscopeDebugHudTelemetry>();
        mTelemetry->rateSampleSequence = 1;
        mTelemetry->actualRateValid = true;
        mTelemetry->actualCompletedUps = 2'400.0;
        mTelemetry->stageTimings.sequence = 1;
        for (std::size_t index = 0;
                index < mTelemetry->stageTimings.stageNanos.size(); ++index) {
            const double value = 8'000.0
                    + static_cast<double>(index) * 1'250.0;
            mTelemetry->stageTimings.stageNanos[index] = value;
            mTelemetry->stageTimings.totalNanos += value;
        }
        return SourceSurfaceResult::Applied;
    } catch (...) {
        mAdapter.reset();
        mFrame.reset();
        mTelemetry.reset();
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult VulkanDemoFrameProducer::unbindBackend() noexcept {
    mTelemetry.reset();
    mFrame.reset();
    mAdapter.reset();
    return SourceSurfaceResult::Applied;
}

SourceSurfaceResult VulkanDemoFrameProducer::onSurfaceAttached(
        std::uint64_t,
        int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return SourceSurfaceResult::Failed;
    }
    mWidth = width;
    mHeight = height;
    mSurfaceAttached = true;
    return SourceSurfaceResult::Applied;
}

SourceSurfaceResult VulkanDemoFrameProducer::onSurfaceResized(
        int width, int height) noexcept {
    if (!mSurfaceAttached || width <= 0 || height <= 0) {
        return SourceSurfaceResult::Failed;
    }
    mWidth = width;
    mHeight = height;
    return SourceSurfaceResult::Applied;
}

SourceSurfaceResult VulkanDemoFrameProducer::onSurfaceDetached(
        SurfaceDetachReason) noexcept {
    mSurfaceAttached = false;
    mWidth = 0;
    mHeight = 0;
    return SourceSurfaceResult::Applied;
}

SourceFrameResult VulkanDemoFrameProducer::renderFrame() noexcept {
    try {
        if (!mSurfaceAttached || mAdapter == nullptr || mFrame == nullptr) {
            return {SourceFrameStatus::Completed,
                    cellularity::render::makeRenderFrameResult(
                            cellularity::render::RenderFrameOutcome::
                                    SkippedNotReady)};
        }
        const auto presentation = cellularity::render::vulkan::
                makeVulkanBasicCellDemoPresentation(mWidth, mHeight);
        return {SourceFrameStatus::Completed,
                mAdapter->presentMicroscope(presentation, *mFrame,
                        mPerformanceHudEnabled ? mTelemetry.get() : nullptr)};
    } catch (...) {
        return {SourceFrameStatus::Failed, {}};
    }
}

} // namespace cellularity::app::render_worker::sources
