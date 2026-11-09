#pragma once

// The preview camera is part of this interface, so its type has to be complete here rather
// than forward-declared: a nested type cannot be named without its enclosing class.
#include "app/genome_preview/GenomePreviewController.h"

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
    /**
     * Hands the preview the genome it should be showing.
     *
     * The phone publishes content and view state separately and then asks the preview to draw,
     * which is what makes an interactive preview interactive: the world keeps its camera between
     * frames, and a person's pan or zoom is an update to the view rather than a new picture. The
     * desktop preview used to take a single still of the organism instead, which is why it had no
     * camera to move. This is the same publication, on the same endpoint, so the two platforms
     * share the mechanism rather than resembling each other.
     *
     * Returns the status the endpoint reported, encoded the way the phone encodes it.
     */
    /**
     * Tells the preview where its view stands: which gene is selected, whether the grid and the
     * tree are on, and how large the viewport is. Published separately from the content because it
     * is what changes while somebody is looking -- a pan, a zoom, a selection -- while the genome
     * itself stays put.
     */
    [[nodiscard]] std::uint32_t publishGenomePreviewViewState(
            int selectedGeneIndex,
            bool gridEnabled,
            bool treeEnabled,
            int viewportWidth,
            int viewportHeight);

    /**
     * Hands the preview one thing the person did: move, zoom, select a gene, take a ghost.
     *
     * The kinds are numbered exactly as the phone numbers them, so the two platforms agree on what
     * a message means rather than on what it happens to be called.
     */
    [[nodiscard]] std::uint32_t enqueueGenomePreviewInteraction(
            int kind, float first, float second, float third);

    [[nodiscard]] std::uint32_t publishGenomePreviewContent(
            const std::uint8_t* genomeDraftBytes,
            std::size_t genomeDraftByteCount,
            double simulationTimeHours);

    /**
     * Where the preview stands: how many Cells it grew, which gene a tap chose, and where the
     * camera is.
     *
     * One call rather than five, because these are one snapshot: a Cell count from before a
     * selection, read beside the selection itself, would describe two different previews. The
     * camera travels as the bit patterns of its three floats for the same reason the plate's food
     * mass does -- an integer vector cannot hold a fraction, and a rounded camera is a camera
     * nobody set.
     *
     * Nine numbers, in this order: the Cell count, the content revision it was counted at, the
     * preview's Cell allocation, the selection token, the selected gene index, the viewport
     * revision, then the camera's centre X, centre Y and zoom.
     */
    [[nodiscard]] std::array<std::int64_t, 9> genomePreviewStatus() const;

    [[nodiscard]] std::vector<std::uint8_t> captureGenomeFilePreviewRgba(
            const cellularity::GenomeData& genome,
            double maximumSimulationTime,
            const GenomePreviewController::Camera* camera = nullptr);
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
    // Returns whether the gesture was taken into the queue: a refusal is an ordinary answer --
    // the queue may be closed, or the viewport may have been replaced under the caller -- and a
    // host that cannot tell it from a placement has no way to say so on screen.
    bool
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
    // Draws the same frame into memory the caller owns, as tightly packed top-left RGBA8 at
    // the caller's row stride, and reports how many bytes it wrote.
    //
    // This is the entry point a host uses when it draws the world itself instead of letting a
    // window present it: the frame becomes one layer of that host's own scene, which is the only
    // way the world can sit *below* the rest of the interface rather than in front of it. A
    // window cannot do that, because a window is a sibling of the interface's surface and not a
    // layer inside it.
    // The preview's frame into caller memory, for the same reason the world has one: a preview that
    // is a layer of the host's scene cannot be presented by a window of its own.
    [[nodiscard]] std::size_t drawGenomePreviewIntoRgba(std::uint8_t* destination,
            std::size_t destinationSize, std::size_t rowStride, bool& wrote);
    [[nodiscard]] cellularity::render::RenderFrameResult
    drawMicroscopeIntoRgba(std::uint8_t* destination,
            std::size_t destinationSize, std::size_t rowStride,
            std::size_t& written);
    [[nodiscard]] cellularity::render::RenderFrameResult
    drawMicroscopeForCurrentMode();
    void markMicroscopeFramePresented();
    void resetMicroscopeInputTimelineMeasurements();
    [[nodiscard]] cellularity::microscope::InputTimelineSnapshot
    microscopeInputTimelineSnapshot() const;

private:
    // Which of the two places a drawn frame goes: the surface a window presents, or a buffer
    // the host will draw itself. One function serves both so the two paths cannot drift apart
    // in what they draw -- only in where the pixels end up.
    enum class FrameDestination { Surface, RgbaBuffer };
    [[nodiscard]] cellularity::render::RenderFrameResult
    drawMicroscopeInto(FrameDestination destination, std::uint8_t* bytes,
            std::size_t byteCount, std::size_t rowStride, std::size_t& written);

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
    // Publication order is monotonic per controller, so a late update can be recognised as late.
    std::uint64_t mGenomePreviewProducerSequence = 0;
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
