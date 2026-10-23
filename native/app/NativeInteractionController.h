#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "app/NativeWorldContext.h"
#include "app/microscope/MicroscopeCommandMailbox.h"
#include "app/microscope/MicroscopeFrameController.h"
#include "app/microscope/MicroscopeInputBatchLayout.h"
#include "app/microscope/MicroscopeInputTimeline.h"
#include "app/microscope/MicroscopePresentationState.h"
#include "core/CellData.h"
#include "core/Gene.h"

class MicroscopeInputController;
class NativeSimulationStepper;
class NativeViewport;

namespace cellularity::microscope {
class MicroscopeSimulationState;
class MicroscopeWorldInteractionState;
} // namespace cellularity::microscope

// Owns presentation and queued-input state. World-facing interaction and
// simulation cadence are supplied only through a simulation-owner lease.
class NativeInteractionController {
public:
    NativeInteractionController(float cameraBoundsRadius,
            std::uint64_t initialWorldEpoch);
    ~NativeInteractionController();

    void setViewportSurfaceSize(int width, int height);
    void setViewportCamera(float cameraX, float cameraY, float zoom);
    void moveViewportByScreenDelta(float dx, float dy);
    void zoomViewportAt(float factor, float screenX, float screenY);
    void abandonViewportSurface();
    void publishViewportIfChanged();
    // The final element reports whether native fling inertia is still active.
    // Reading it with the camera values under one lock prevents presentation
    // cadence from stopping before the owner applies a newly accepted fling.
    [[nodiscard]] std::array<float, 7> viewportState() const;
    [[nodiscard]] std::uint64_t viewportRevision() const;
    [[nodiscard]] float viewportScreenToWorldX(float screenX) const;
    [[nodiscard]] float viewportScreenToWorldY(float screenY) const;
    void invalidateFrameData();
    void onTemperatureModeChanged(
            int temperatureMode,
            cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction);
    bool stepFrame(
            NativeWorldContext context, NativeSimulationStepper& simulation,
            cellularity::microscope::MicroscopeSimulationState& simulationState,
            cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction,
            const MicroscopeFrameController::RenderFrameCapture& captureRenderFrame);
    bool stepViewportInertia();
    [[nodiscard]] bool viewportInertiaActive() const;
    void setViewportFlingVelocity(float velocityX, float velocityY);
    void clearInputQueues(cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction);
    void queueTouchDown(int pointerId, float screenX, float screenY);
    void queueTouchMove(int pointerId, float screenX, float screenY);
    void queueTouchUp(int pointerId);
    void queueTap(float screenX, float screenY,
            const cellularity::GenomeData* genome);
    bool queuePan(float dx, float dy);
    bool queueZoom(float factor, float screenX, float screenY);
    bool queueFling(float velocityX, float velocityY);
    void submitInputBatch(
            const cellularity::microscope::MicroscopeInputBatch& batch,
            std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                    tapGenome,
            cellularity::microscope::MicroscopeInputBatchResult& result,
            bool rejectTapForStalePlacementGenome = false);
    [[nodiscard]] cellularity::microscope::PresentationMotionSnapshot
    presentationMotionSnapshot() const noexcept;
    [[nodiscard]] bool requestInputResync() noexcept;
    [[nodiscard]] std::uint32_t activePointerGenerationCount() const noexcept;
    void closeInputAdmission() noexcept;
    [[nodiscard]] bool inputAdmissionClosed() const noexcept;
    NativeViewport& viewport();
    const NativeViewport& viewport() const;
    void synchronizePresentationInteraction(
            const cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction) noexcept;
    [[nodiscard]] bool tryConsumeNewestPresentationViewport(
            cellularity::microscope::PresentationViewportSnapshot& snapshot) noexcept;
    [[nodiscard]] bool tryConsumeNewestOwnerViewport(
            cellularity::microscope::PresentationViewportSnapshot& snapshot) noexcept;
    [[nodiscard]] bool publishOwnerCameraIfCurrent(
            std::uint64_t sourceViewportRevision, float cameraX, float cameraY,
            float zoom) noexcept;
    [[nodiscard]] cellularity::microscope::MicroscopePresentationStateSnapshot
    presentationStateSnapshot() const noexcept;
    [[nodiscard]] cellularity::microscope::MicroscopeCommandMailbox&
    commandMailbox() noexcept;
    [[nodiscard]] const cellularity::microscope::MicroscopeCommandMailbox&
    commandMailbox() const noexcept;
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerTouchDown(int pointerId, float screenX, float screenY);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerTouchMove(int pointerId, float screenX, float screenY,
            bool pathDependent);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerTouchUp(int pointerId);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerTap(
            float screenX, float screenY,
            std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                    genome);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerTemperatureMode(int temperatureMode);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerToolOrdinal(int toolOrdinal);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerSlowMotionSpeed(float slowMotionSpeed);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerOptionalTargetUps(double targetUps);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerLifecycle(bool paused);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerBarrier(
            cellularity::microscope::MicroscopeWorldCommandKind kind,
            std::uint64_t requestId,
            std::shared_ptr<cellularity::microscope::MicroscopeCommandCompletion>
                    completion,
            std::shared_ptr<cellularity::microscope::MicroscopeCommandPayload>
                    payload);
    [[nodiscard]] cellularity::microscope::MicroscopeCommandEnqueueResult
    enqueueOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind kind,
            std::uint64_t requestId,
            std::shared_ptr<cellularity::microscope::MicroscopeCommandCompletion>
                    completion,
            std::shared_ptr<cellularity::microscope::MicroscopeCommandPayload>
                    payload);
    void wakeOwner() noexcept;
    void advanceCommandWorldEpoch(std::uint64_t worldEpoch);
    [[nodiscard]] bool consumeViewportCaptureHint() noexcept;
    std::array<float, 3> nextBackgroundColor();
    void setPresentationTemperatureMode(int temperatureMode) noexcept;
    [[nodiscard]] int presentationTemperatureMode() const noexcept;
    bool shouldUploadDrawFrame() const;
    void markDrawFrameConsumed();
    void paceBeforeDraw();
    [[nodiscard]] cellularity::microscope::ResolvedInputState
    resolvedInputState() const noexcept;
    void markFramePresented(std::uint64_t frameLastResolvedInputSequence,
            std::uint64_t frameWorldRevision,
            std::int64_t sourceCapturedNanos,
            std::int64_t framePreparedNanos,
            std::uint64_t presentedViewportRevision,
            std::int64_t firstPresentedNanos);
    void resetInputTimelineMeasurements();
    [[nodiscard]] cellularity::microscope::InputTimelineSnapshot
    inputTimelineSnapshot() const;

private:
    void publishViewport(std::uint64_t inputSequence,
            bool requestOwnerCapture) noexcept;
    void refreshPresentationMotionLocked() noexcept;

    std::unique_ptr<NativeViewport> mViewport;
    std::unique_ptr<MicroscopeFrameController> mFrame;
    std::unique_ptr<MicroscopeInputController> mInput;
    cellularity::microscope::MicroscopeInputTimeline mInputTimeline;
    cellularity::microscope::MicroscopePresentationState mPresentationState;
    cellularity::microscope::MicroscopeCommandMailbox mCommandMailbox;
    mutable std::mutex mPresentationMutex;
    mutable std::mutex mIngressMutex;
    std::uint64_t mLastPublishedViewportRevision = 0;
    cellularity::microscope::PresentationMotionSnapshot
            mPresentationMotionSnapshot{};
    std::atomic<bool> mInputAdmissionClosed{false};
    std::atomic<int> mPresentationTemperatureMode{
            MicroscopeFrameController::MODE_FREEZE
    };
};
