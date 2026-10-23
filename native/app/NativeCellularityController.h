#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/NativeSimulationOwner.h"
#include "app/NativeWorldContext.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeInputBatchLayout.h"
#include "app/microscope/MicroscopeInputTimeline.h"
#include "app/microscope/MicroscopePresentationState.h"
#include "app/microscope/MicroscopeRuntimeStartConfiguration.h"
#include "core/GenomePool.h"
#include "render/backend/RenderBackendKind.h"
#include "render/backend/RenderFrameResult.h"

class NativeInteractionController;
class NativeRenderAssets;
class NativeRenderSession;
class NativeRenderTextureLoader;

namespace cellularity::app {
class EnvironmentFieldPreviewEndpoint;

namespace genome_preview {
class GenomePreviewEndpoint;
} // namespace genome_preview

namespace microscope {
class SubstratePreviewCaptureEndpoint;
} // namespace microscope
} // namespace cellularity::app

namespace cellularity::microscope {
class NativeMicroscopeRuntime;
class NativeMicroscopeRequest;
struct NativeMicroscopeRuntimeSnapshot;
} // namespace cellularity::microscope

namespace cellularity::render {
class RenderSurfaceSession;
} // namespace cellularity::render

// Composition root. NativeSimulationOwner is the sole storage owner for the
// mutable world and world-facing interaction state. Android calls enter
// through a serialized JNI bridge; the microscope runtime owns simulation
// updates after startup.
class NativeCellularityController {
public:
    NativeCellularityController(bool isPreviewMode,
            const Environment& environment);
    explicit NativeCellularityController(std::unique_ptr<SimulationWorld> world);
    ~NativeCellularityController();

    [[nodiscard]] NativeSimulationOwner::ReadAccess readWorld() const;
    [[nodiscard]] NativeSimulationOwner::WriteAccess writeWorld();
    bool isPreviewMode() const;
    [[nodiscard]] bool hasRenderSurfaceSession() const noexcept;
    void installRenderSurfaceSession(
            std::unique_ptr<cellularity::render::RenderSurfaceSession>
                    surfaceSession);
    // Installs a worker-owned forwarding adapter without touching the physical
    // surface. The native Vulkan worker is the sole owner of attach/resize.
    void installWorkerRenderSurfaceSession(
            std::unique_ptr<cellularity::render::RenderSurfaceSession>
                    surfaceSession);
    [[nodiscard]] bool uninstallRenderSurfaceSession(
            cellularity::render::RenderBackendKind backendKind,
            const void* bindingIdentity) noexcept;
    [[nodiscard]] cellularity::render::RenderBackendKind renderBackendKind() const;
    bool hasRendererResources() const;
    bool hasRendererGpuBuffers() const;
    int rendererVisibleCellCount() const;
    [[nodiscard]] std::array<std::uint64_t, 4>
    microscopePreparationFailureState() const noexcept;

    void setViewportSurfaceSize(int width, int height);
    void setViewportPresentationSize(int width, int height);
    void setViewportCamera(float cameraX, float cameraY, float zoom);
    void moveViewportByScreenDelta(float dx, float dy);
    void zoomViewportAt(float factor, float screenX, float screenY);
    float viewportScreenToWorldX(float screenX) const;
    float viewportScreenToWorldY(float screenY) const;
    std::array<float, 7> viewportState() const;
    float viewportCameraX() const;
    float viewportCameraY() const;
    void setGenomePreviewGridEnabled(bool enabled);
    void setGenomePreviewTreeEnabled(bool enabled);
    void setGenomePreviewSelectedGeneIndex(int selectedGeneIndex);

    void loadRendererResources(const NativeRenderAssets& assets,
            NativeRenderTextureLoader& textureLoader);
    void releaseRendererResources();
    void abandonRendererResources();
    [[nodiscard]] std::uint64_t genomePreviewSurfaceCreated() noexcept;
    void genomePreviewSurfaceDestroyed(
            std::uint64_t attachmentGeneration) noexcept;
    [[nodiscard]] std::uint64_t substratePreviewSurfaceCreated() noexcept;
    void substratePreviewSurfaceDestroyed(
            std::uint64_t attachmentGeneration) noexcept;

    void update(bool isForRendering, double speed);
    void update1(bool isForRendering, double speed);
    void update2(bool forView, double speed);
    std::uint64_t completedUpdateCount() const;
    void resetCompletedUpdateCount();
    void invalidateRenderWorldEpoch(std::uint64_t worldEpoch);

    void resetGenomePreviewTreeState();
    void beginGenomePreviewTreeStep();
    void recordGenomePreviewTreeSplits();
    void captureGenomePreviewGhost();
    void clearGenomePreviewGhost();
    [[nodiscard]] cellularity::render::RenderFrameResult drawGenomePreview();
    [[nodiscard]] std::vector<std::uint8_t> captureGenomeFilePreviewRgba(
            const cellularity::GenomeData& genome,
            double maximumSimulationTime);
    [[nodiscard]] cellularity::render::RenderFrameResult
    drawEnvironmentFieldPreview();
    [[nodiscard]] std::shared_ptr<
            cellularity::app::EnvironmentFieldPreviewEndpoint>
    environmentFieldPreviewEndpoint() const noexcept;
    [[nodiscard]] std::shared_ptr<
            cellularity::app::genome_preview::GenomePreviewEndpoint>
    genomePreviewEndpoint() const noexcept;
    [[nodiscard]] std::shared_ptr<
            cellularity::app::microscope::SubstratePreviewCaptureEndpoint>
    substratePreviewCaptureEndpoint() const noexcept;
    int selectGenomePreviewGeneAt(double worldX, double worldY);
    int genomePreviewTreeNodeCount() const;
    void invalidateMicroscopeFrameData();
    void setMicroscopeTemperatureMode(int temperatureMode);
    void setMicroscopeToolOrdinal(int toolOrdinal);
    void setMicroscopeSlowMotionSpeed(float slowMotionSpeed);
    void setMicroscopeIncubateTargetUps(double targetUps);
    void setMicroscopePerformanceHudEnabled(bool enabled) noexcept;
    [[nodiscard]] bool microscopePerformanceHudEnabled() const noexcept;
    void startMicroscopeRuntime();
    void startMicroscopeRuntime(
            const cellularity::microscope::MicroscopeRuntimeStartConfiguration&
                    configuration);
    void stopMicroscopeRuntime() noexcept;
    [[nodiscard]] bool microscopeRuntimeActive() const noexcept;
    void setMicroscopeLifecyclePaused(bool paused);
    [[nodiscard]] cellularity::microscope::NativeMicroscopeRuntimeSnapshot
    microscopeRuntimeSnapshot() const;
    [[nodiscard]] cellularity::microscope::MicroscopeRuntimeStartConfiguration
    microscopeRuntimeStartConfiguration();
    void resetMicroscopeWorld();
    [[nodiscard]] std::uint64_t
    writeMicroscopeSubstratePath(const std::string& path);
    [[nodiscard]] std::uint64_t
    writeSubstratePathAndPreparePreview(const std::string& path);
    [[nodiscard]] std::vector<std::uint8_t>
    captureSubstratePreviewRgba(std::uint64_t token);
    void discardSubstratePreviewFrame(std::uint64_t token) noexcept;
    [[nodiscard]] std::vector<std::uint8_t>
    applyMicroscopeEnvironmentPatch(std::vector<std::uint8_t> patchBytes);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    inspectMicroscopeCell(int cellId);
    [[nodiscard]] std::vector<std::uint8_t>
    inspectMicroscopeEnvironment();
    [[nodiscard]] cellularity::microscope::NativeMicroscopeRuntimeSnapshot
    inspectMicroscopeStats();
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    inspectMicroscopeSelectedGenome();
    bool stepMicroscopeFrame();
    bool stepMicroscopeViewportInertia();
    void setMicroscopeViewportFlingVelocity(float velocityX, float velocityY);
    void clearMicroscopeInputQueues();
    void queueMicroscopeTouchDown(int pointerId, float screenX, float screenY);
    void queueMicroscopeTouchMove(int pointerId, float screenX, float screenY);
    void queueMicroscopeTouchUp(int pointerId);
    void
    queueMicroscopeTap(float screenX, float screenY,
            const cellularity::GenomeData* genome);
    bool queueMicroscopePan(float dx, float dy);
    bool queueMicroscopeZoom(float factor, float screenX, float screenY);
    bool queueMicroscopeFling(float velocityX, float velocityY);
    void submitMicroscopeInputBatch(
            const cellularity::microscope::MicroscopeInputBatch& batch,
            std::uint64_t authoritativeInputEpoch,
            std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                    tapGenome,
            cellularity::microscope::MicroscopeInputBatchResult& result,
            bool rejectTapForStalePlacementGenome = false);
    [[nodiscard]] cellularity::microscope::PresentationMotionSnapshot
    microscopePresentationMotionSnapshot() const noexcept;
    [[nodiscard]] bool requestMicroscopeInputResync() noexcept;
    [[nodiscard]] std::uint32_t
    microscopeActivePointerGenerationCount() const noexcept;
    void closeMicroscopeInputAdmission() noexcept;
    [[nodiscard]] cellularity::render::RenderFrameResult
    clearMicroscopeNoRender();
    [[nodiscard]] cellularity::render::RenderFrameResult drawMicroscope();
    [[nodiscard]] cellularity::render::RenderFrameResult
    drawMicroscopeForCurrentMode();
    void markMicroscopeFramePresented();
    void resetMicroscopeInputTimelineMeasurements();
    [[nodiscard]] cellularity::microscope::InputTimelineSnapshot
    microscopeInputTimelineSnapshot() const;

private:
    const bool mPreviewMode;
    // Declared before render-facing owners so render preparation is stopped and
    // joined before the owned world is destroyed.
    NativeSimulationOwner mSimulationOwner;
    std::unique_ptr<NativeInteractionController> mInteraction;
    std::unique_ptr<NativeRenderSession> mRenderSession;
    std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
            mEnvironmentFieldPreviewEndpoint;
    std::shared_ptr<cellularity::app::genome_preview::GenomePreviewEndpoint>
            mGenomePreviewEndpoint;
    std::shared_ptr<cellularity::app::microscope::SubstratePreviewCaptureEndpoint>
            mSubstratePreviewCaptureEndpoint;
    cellularity::microscope::PresentationViewportSnapshot
            mPresentedViewportSnapshot{};
    bool mHasPresentedViewportSnapshot = false;
    std::atomic<bool> mPerformanceHudEnabled{false};
    std::unique_ptr<cellularity::microscope::NativeMicroscopeRuntime>
            mMicroscopeRuntime;
    std::atomic<std::uint64_t> mNextMicroscopeRequestId{1};

    NativeWorldContext worldContext(SimulationWorld& world);
    void executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind kind,
            const std::shared_ptr<cellularity::microscope::NativeMicroscopeRequest>& request,
            bool lifecycleBarrier);

    friend class cellularity::app::genome_preview::GenomePreviewEndpoint;
};
