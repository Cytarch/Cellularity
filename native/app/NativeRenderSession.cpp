#include "app/NativeRenderSession.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "app/EnvironmentFieldPreviewState.h"
#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/render/MicroscopeRenderFramePipeline.h"
#include "core/SimulationWorld.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderSourceFrame.h"

namespace {

constexpr float kSubstratePreviewDishMargin = 1.04f;

std::uint64_t nextPreviewToken(std::uint64_t current) {
    return current == std::numeric_limits<std::uint64_t>::max() ? 1
                                                                : current + 1;
}

cellularity::render::RenderFrameResult validatePrimaryBackendResult(
        cellularity::render::RenderFrameResult result,
        const char* operation) {
    if (!cellularity::render::isValidRenderFrameResult(result)
            || result.semanticValue != 0
            || (result.outcome
                            == cellularity::render::RenderFrameOutcome::Presented
                    && result.flags
                            != cellularity::render::kPrimaryContentPresented)) {
        throw std::logic_error(std::string(operation)
                + " returned an invalid backend frame result");
    }
    return result;
}

} // namespace

struct NativeRenderSession::SubstratePreviewState final {
    struct FrameData final {
        explicit FrameData(int maxCellCount)
                : sourceCapture(maxCellCount, sourceFrame),
                  frameBuilder(maxCellCount, preparedFrame) {}

        cellularity::render::RenderSourceFrame sourceFrame;
        cellularity::render::RenderSourceCapture sourceCapture;
        cellularity::render::PreparedRenderFrame preparedFrame;
        cellularity::render::RenderFrameBuilder frameBuilder;
    };

    std::mutex mutex;
    std::unique_ptr<cellularity::render::PreparedRenderFrame> frame;
    std::uint64_t lastIssuedToken = 0;
    std::uint64_t activeToken = 0;
};

NativeRenderSession::NativeRenderSession(int maxCellCount,
        std::unique_ptr<cellularity::render::RenderSurfaceSession>
                surfaceSession)
        : mMaxCellCount(maxCellCount),
          mSurfaceSession(std::move(surfaceSession)),
          mSubstratePreview(std::make_unique<SubstratePreviewState>()) {}

NativeRenderSession::~NativeRenderSession() {
    // Join preparation before any GPU-facing owner is destroyed. Backend
    // resources are detached/released by the surface lifecycle; no worker ever
    // touches them.
    mFramePipeline.reset();
}

bool NativeRenderSession::hasSurfaceSession() const noexcept {
    return mSurfaceSession != nullptr;
}

void NativeRenderSession::installSurfaceSession(
        std::unique_ptr<cellularity::render::RenderSurfaceSession>
                surfaceSession) {
    if (surfaceSession == nullptr) {
        throw std::invalid_argument("Render surface session cannot be null");
    }
    if (mSurfaceSession != nullptr && (mSurfaceSession->hasResources() || mSurfaceSession->hasGpuBuffers())) {
        throw std::logic_error(
                "Cannot replace an attached render surface session");
    }
    mSurfaceSession = std::move(surfaceSession);
}

bool NativeRenderSession::uninstallSurfaceSession(
        cellularity::render::RenderBackendKind backendKind,
        const void* bindingIdentity) noexcept {
    if (mSurfaceSession == nullptr || mSurfaceSession->kind() != backendKind
            || mSurfaceSession->bindingIdentity() != bindingIdentity) {
        return false;
    }
    if (mSurfaceSession->hasResources()
            || mSurfaceSession->hasGpuBuffers()) {
        return false;
    }
    mSurfaceSession.reset();
    return true;
}

cellularity::render::RenderBackendKind NativeRenderSession::backendKind() const {
    return surfaceSession().kind();
}

cellularity::render::RenderSurfaceSession& NativeRenderSession::surfaceSession() {
    if (mSurfaceSession == nullptr) {
        throw std::logic_error("Render surface session is not installed");
    }
    return *mSurfaceSession;
}

const cellularity::render::RenderSurfaceSession&
NativeRenderSession::surfaceSession() const {
    if (mSurfaceSession == nullptr) {
        throw std::logic_error("Render surface session is not installed");
    }
    return *mSurfaceSession;
}

cellularity::microscope::MicroscopeRenderFramePipeline&
NativeRenderSession::framePipeline() {
    if (mFramePipeline == nullptr) {
        mFramePipeline = std::make_unique<
                cellularity::microscope::MicroscopeRenderFramePipeline>(mMaxCellCount);
    }
    return *mFramePipeline;
}

bool NativeRenderSession::hasResources() const {
    return mSurfaceSession != nullptr && mSurfaceSession->hasResources();
}

bool NativeRenderSession::hasBackendGpuBuffers() const {
    return mSurfaceSession != nullptr && mSurfaceSession->hasGpuBuffers();
}

void NativeRenderSession::initializeMicroscopePipeline() {
    (void)framePipeline();
}

void NativeRenderSession::loadResources(
        const SimulationWorld& world, const NativeRenderAssets& assets,
        NativeRenderTextureLoader& textureLoader) {
    loadResources(world.mEnvironment.mIsPreviewMode, assets, textureLoader);
}

void NativeRenderSession::loadResources(
        bool previewMode, const NativeRenderAssets& assets,
        NativeRenderTextureLoader& textureLoader) {
    surfaceSession().attachSurface(previewMode, assets, textureLoader);
}

void NativeRenderSession::resizeSurface(int width, int height) {
    if (mSurfaceSession != nullptr) {
        mSurfaceSession->resizeSurface(width, height);
    }
}

void NativeRenderSession::releaseResources() {
    if (mSurfaceSession != nullptr) {
        mSurfaceSession->detachSurface();
    }
}

void NativeRenderSession::abandonResources() {
    if (mSurfaceSession != nullptr) {
        mSurfaceSession->abandonSurface();
    }
}

void NativeRenderSession::invalidateWorldEpoch(std::uint64_t worldEpoch) {
    if (mFramePipeline != nullptr) {
        mFramePipeline->invalidateWorldEpoch(worldEpoch);
    }
}

void NativeRenderSession::invalidateModeGeneration(
        std::uint64_t modeGeneration) {
    if (mFramePipeline != nullptr) {
        mFramePipeline->invalidateModeGeneration(modeGeneration);
    }
}

bool NativeRenderSession::consumeUrgentMicroscopeCaptureRequest() noexcept {
    return mFramePipeline != nullptr && mFramePipeline->consumeUrgentCaptureRequest();
}

std::array<std::uint64_t, 4>
NativeRenderSession::microscopePreparationFailureState() const noexcept {
    if (mFramePipeline == nullptr) {
        return {};
    }
    const auto failure = mFramePipeline->failure();
    return {static_cast<std::uint64_t>(failure.kind),
            static_cast<std::uint64_t>(failure.stage), failure.worldEpoch,
            failure.sourceSequence};
}

cellularity::microscope::MicroscopeRenderFrameExchangeTelemetry
NativeRenderSession::microscopeFrameExchangeTelemetry() const noexcept {
    return mFramePipeline == nullptr
            ? cellularity::microscope::MicroscopeRenderFrameExchangeTelemetry{}
            : mFramePipeline->exchangeTelemetry();
}

int NativeRenderSession::captureMicroscopeFrame(
        const SimulationWorld& world, const NativeViewport& viewport, float margin,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    return captureViewport(world, viewport, margin, captureMetadata);
}

std::uint64_t NativeRenderSession::prepareSubstratePreviewFrame(
        const SimulationWorld& world,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    const double dishRadius = world.mEnvironment.geometry().radius();
    if (!std::isfinite(dishRadius) || dishRadius <= 0.0 || dishRadius > static_cast<double>(std::numeric_limits<float>::max()) / kSubstratePreviewDishMargin) {
        throw std::invalid_argument(
                "Substrate preview requires a positive finite dish radius");
    }

    const float framedRadius =
            static_cast<float>(dishRadius) * kSubstratePreviewDishMargin;
    NativeViewport viewport(framedRadius);
    viewport.setCameraBoundsEnabled(false);
    viewport.setSurfaceSize(SUBSTRATE_PREVIEW_PIXELS,
            SUBSTRATE_PREVIEW_PIXELS);
    viewport.setCamera(0.0f, 0.0f, 1.0f / framedRadius);
    const auto nativeBounds = viewport.viewBounds(0.0f);
    const cellularity::render::RenderViewBounds bounds =
            cellularity::app::toRenderBounds(nativeBounds);
    captureMetadata.stage =
            cellularity::render::RenderCaptureStage::SubstrateFilePreview;
    captureMetadata.viewport =
            cellularity::app::snapshotViewport(viewport, bounds);

    std::lock_guard<std::mutex> lock(mSubstratePreview->mutex);
    // Invalidate any older request before mutating the reusable frame storage.
    // lastIssuedToken deliberately survives capture/discard so a delayed
    // surface event can never consume a later save frame after a timeout.
    mSubstratePreview->activeToken = 0;
    mSubstratePreview->frame.reset();
    const std::uint64_t pendingToken =
            nextPreviewToken(mSubstratePreview->lastIssuedToken);
    try {
        // Capture and geometry-building scratch are useful only at the owner save
        // point. Move out the immutable prepared payload so the source copy and
        // builder workspace are released before waiting for the render thread.
        SubstratePreviewState::FrameData preparation(mMaxCellCount);
        preparation.sourceCapture.capture(world, bounds, captureMetadata,
                preparation.sourceFrame);
        preparation.frameBuilder.build(preparation.sourceFrame,
                preparation.preparedFrame);
        preparation.preparedFrame.valid = true;
        // The short-lived builder starts its local sequence at the same value on
        // every save. Replace all GPU upload revisions with the session token so
        // allocator address reuse can never make a later preview look unchanged.
        preparation.preparedFrame.identity.captureSequence = pendingToken;
        preparation.preparedFrame.layout.geometryRevision = pendingToken;
        preparation.preparedFrame.layout.instanceRevision = pendingToken;
        preparation.preparedFrame.layout.markerRevision = pendingToken;
        preparation.preparedFrame.interpolation.sourceCaptureSequence =
                pendingToken;
        preparation.preparedFrame.interpolation.targetCaptureSequence =
                pendingToken;
        mSubstratePreview->frame =
                std::make_unique<cellularity::render::PreparedRenderFrame>(
                        std::move(preparation.preparedFrame));
    } catch (...) {
        mSubstratePreview->frame.reset();
        throw;
    }
    mSubstratePreview->lastIssuedToken = pendingToken;
    mSubstratePreview->activeToken = mSubstratePreview->lastIssuedToken;
    return mSubstratePreview->activeToken;
}

std::uint64_t NativeRenderSession::tryPrepareSubstratePreviewFrame(
        const SimulationWorld& world,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) noexcept {
    try {
        return prepareSubstratePreviewFrame(world, captureMetadata);
    } catch (...) {
        std::lock_guard<std::mutex> lock(mSubstratePreview->mutex);
        mSubstratePreview->frame.reset();
        mSubstratePreview->activeToken = 0;
        return 0;
    }
}

std::vector<std::uint8_t> NativeRenderSession::captureSubstratePreviewRgba(
        std::uint64_t token,
        const std::array<float, 3>& backgroundColor) {
    std::unique_ptr<cellularity::render::PreparedRenderFrame> ownedFrame;
    {
        std::lock_guard<std::mutex> lock(mSubstratePreview->mutex);
        if (token == 0 || token != mSubstratePreview->activeToken || mSubstratePreview->frame == nullptr || !mSubstratePreview->frame->valid || !mSubstratePreview->frame->geometryReady) {
            return {};
        }
        // Consume the one-shot token before touching the backend. A timeout can now
        // discard or stage another preview without waiting for draw/readback.
        ownedFrame = std::move(mSubstratePreview->frame);
        mSubstratePreview->activeToken = 0;
    }
    const auto& frame = *ownedFrame;

    auto& backend = surfaceSession();
    backend.beginOffscreenRgba(
            cellularity::render::OffscreenRenderTarget::SubstratePreview,
            SUBSTRATE_PREVIEW_PIXELS, SUBSTRATE_PREVIEW_PIXELS);
    try {
        cellularity::render::RenderPresentationState presentation;
        presentation.viewport = frame.viewport;
        presentation.backgroundColor = backgroundColor;
        if (backend.drawMicroscopeOffscreenPass(presentation, frame)
                != cellularity::render::RenderPassStatus::Completed) {
            throw std::runtime_error(
                    "Production renderer rejected the substrate preview frame");
        }
        return backend.finishOffscreenRgba(
                cellularity::render::OffscreenRenderTarget::SubstratePreview);
    } catch (...) {
        backend.cancelOffscreenRgba(
                cellularity::render::OffscreenRenderTarget::SubstratePreview);
        throw;
    }
}

void NativeRenderSession::discardSubstratePreviewFrame(
        std::uint64_t token) noexcept {
    std::lock_guard<std::mutex> lock(mSubstratePreview->mutex);
    if (token != 0 && token == mSubstratePreview->activeToken) {
        mSubstratePreview->frame.reset();
        mSubstratePreview->activeToken = 0;
    }
}

int NativeRenderSession::captureMicroscopeFrame(
        const SimulationWorld& world,
        const cellularity::render::RenderViewportState& viewport, float margin,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0 || viewport.viewWidth == 0.0f || viewport.viewHeight == 0.0f) {
        return 0;
    }

    cellularity::render::RenderViewBounds coverage = viewport.visibleBounds;
    coverage.minX -= margin;
    coverage.maxX += margin;
    coverage.minY -= margin;
    coverage.maxY += margin;
    captureMetadata.viewport = viewport;
    captureMetadata.viewport.coverageRevision = viewport.revision;
    captureMetadata.viewport.coverageBounds = coverage;

    cellularity::render::RenderMarkerCaptureRequest markerRequest;
    std::size_t markerRequestCount = 0;
    if (!world.mEnvironment.mIsPreviewMode) {
        const int selectedCellIndex = world.mCellData.getIdFromSelectTag(1);
        if (selectedCellIndex != -1 && !world.mCellData.mIsDead[selectedCellIndex]) {
            markerRequest = {selectedCellIndex, 0.01f, 1.0f};
            markerRequestCount = 1;
        }
    }
    const auto* requests = markerRequestCount == 0 ? nullptr : &markerRequest;
    return framePipeline().capture(world, coverage, captureMetadata, requests,
            markerRequestCount);
}

int NativeRenderSession::captureViewport(
        const SimulationWorld& world, const NativeViewport& viewport, float margin,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    if (!viewport.isReady() || viewport.viewWidth() == 0.0f || viewport.viewHeight() == 0.0f) {
        return 0;
    }

    const NativeViewport::ViewBounds viewportBounds = viewport.viewBounds(margin);
    const cellularity::render::RenderViewBounds viewBounds{
            viewportBounds.minX, viewportBounds.maxX, viewportBounds.minY,
            viewportBounds.maxY
    };
    const NativeViewport::ViewBounds visibleViewportBounds =
            viewport.viewBounds(0.0f);
    captureMetadata.viewport =
            cellularity::app::snapshotViewport(viewport, viewBounds);
    captureMetadata.viewport.visibleBounds =
            cellularity::app::toRenderBounds(visibleViewportBounds);

    cellularity::render::RenderMarkerCaptureRequest markerRequest;
    std::size_t markerRequestCount = 0;
    if (!world.mEnvironment.mIsPreviewMode) {
        const int selectedCellIndex = world.mCellData.getIdFromSelectTag(1);
        if (selectedCellIndex != -1 && !world.mCellData.mIsDead[selectedCellIndex]) {
            markerRequest = {selectedCellIndex, 0.01f, 1.0f};
            markerRequestCount = 1;
        }
    }
    const auto* requests = markerRequestCount == 0 ? nullptr : &markerRequest;
    return framePipeline().capture(world, viewBounds, captureMetadata, requests,
            markerRequestCount);
}

std::optional<cellularity::render::EnvironmentFieldPreviewCommand>
NativeRenderSession::prepareEnvironmentFieldPreview(
        const cellularity::app::EnvironmentFieldPreviewState& state,
        const NativeViewport& viewport) const {
    return state.toRenderCommand(viewport);
}

cellularity::render::RenderFrameResult
NativeRenderSession::drawEnvironmentFieldPreview(
        const cellularity::render::EnvironmentFieldPreviewCommand& command) {
    return validatePrimaryBackendResult(
            surfaceSession().drawEnvironmentFieldPreview(command),
            "Environment Field Preview");
}

cellularity::render::RenderFrameResult NativeRenderSession::drawMicroscope(
        const NativeViewport& viewport,
        const std::array<float, 3>& backgroundColor) {
    if (!viewport.isReady() || viewport.viewWidth() == 0.0f || viewport.viewHeight() == 0.0f) {
        return cellularity::render::makeRenderFrameResult(
                cellularity::render::RenderFrameOutcome::SkippedNotReady);
    }
    const std::array<float, 16> matrix =
            viewport.worldMatrix(cellularity::app::identityMatrix());
    return drawMicroscope(
            cellularity::app::snapshotPresentation(
                    viewport, matrix, backgroundColor));
}

cellularity::render::RenderFrameResult NativeRenderSession::drawMicroscope(
        const cellularity::render::RenderPresentationState& presentation,
        const cellularity::microscope::MicroscopeDebugHudTelemetry*
                debugHudTelemetry) {
    const auto& viewport = presentation.viewport;
    if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0 || viewport.viewWidth == 0.0f || viewport.viewHeight == 0.0f) {
        return cellularity::render::makeRenderFrameResult(
                cellularity::render::RenderFrameOutcome::SkippedNotReady);
    }
    const auto* frame =
            framePipeline().presentLatestPreparedFrame(presentation);
    if (frame == nullptr) {
        return cellularity::render::withPresentedContent(
                validatePrimaryBackendResult(
                        surfaceSession().clearSurface(viewport,
                                {0.31372549019f, 0.31372549019f,
                                        0.31372549019f, 1.0f}),
                        "Microscope fallback clear"),
                cellularity::render::kFallbackClearPresented);
    }
    return validatePrimaryBackendResult(
            surfaceSession().presentMicroscope(
                    framePipeline().latestPresentation(), *frame,
                    debugHudTelemetry),
            "Microscope presentation");
}

cellularity::render::RenderFrameResult
NativeRenderSession::clearMicroscopeNoRender(
        const NativeViewport& viewport) {
    if (!viewport.isReady()) {
        return cellularity::render::makeRenderFrameResult(
                cellularity::render::RenderFrameOutcome::SkippedNotReady);
    }
    cellularity::render::RenderViewportState state;
    state.surfaceWidth = viewport.surfaceWidth();
    state.surfaceHeight = viewport.surfaceHeight();
    return clearMicroscopeNoRender(state);
}

cellularity::render::RenderFrameResult
NativeRenderSession::clearMicroscopeNoRender(
        const cellularity::render::RenderViewportState& viewport) {
    if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0) {
        return cellularity::render::makeRenderFrameResult(
                cellularity::render::RenderFrameOutcome::SkippedNotReady);
    }
    return cellularity::render::withPresentedContent(
            validatePrimaryBackendResult(
                    surfaceSession().clearSurface(viewport,
                            {0.05098039215f, 0.05098039215f,
                                    0.05098039215f, 1.0f}),
                    "No Render clear"),
            cellularity::render::kPrimaryContentPresented);
}

int NativeRenderSession::visibleCellCount() const noexcept {
    return mFramePipeline == nullptr ? 0 : mFramePipeline->visibleCellCount();
}

const cellularity::render::PreparedRenderFrame*
NativeRenderSession::preparedFrame() const {
    return mFramePipeline == nullptr ? nullptr : mFramePipeline->preparedFrame();
}
