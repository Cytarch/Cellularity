#include "app/NativeCellularityController.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "app/EnvironmentFieldPreviewEndpoint.h"
#include "app/NativeGenomePreviewFacade.h"
#include "app/NativeInteractionController.h"
#include "app/NativeRenderSession.h"
#include "app/RenderStateSnapshots.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "app/microscope/NativeMicroscopeRequest.h"
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "app/microscope/SubstratePreviewCaptureEndpoint.h"
#include "app/microscope/render/MicroscopeDebugHud.h"
#include "formats/SubstrateFileCodec.h"
#include "render/frame/RenderFrameTypes.h"
#include "render/frame/RenderTimelineClock.h"

namespace {

constexpr int kMinimumTemperatureMode = cellularity::microscope::
        temperatureModeOrdinal(
                cellularity::microscope::MicroscopeTemperatureMode::Freeze);
constexpr int kMaximumTemperatureMode = cellularity::microscope::
        temperatureModeOrdinal(cellularity::microscope::
                        MicroscopeTemperatureMode::IncubateNoRender);
constexpr int kMinimumToolOrdinal = 0;
constexpr int kMaximumToolOrdinal = 4;
constexpr float kPreviewInitialZoom = 8.0F;

float initialViewportZoom(bool previewMode, double dishRadius) {
    return previewMode
            ? kPreviewInitialZoom
            : 1.0F / static_cast<float>(dishRadius);
}

void validateRuntimeStartConfiguration(
        const cellularity::microscope::MicroscopeRuntimeStartConfiguration&
                configuration) {
    if (configuration.temperatureMode < kMinimumTemperatureMode
            || configuration.temperatureMode > kMaximumTemperatureMode) {
        throw std::invalid_argument(
                "Microscope runtime temperature mode is invalid");
    }
    if (configuration.toolOrdinal < kMinimumToolOrdinal
            || configuration.toolOrdinal > kMaximumToolOrdinal) {
        throw std::invalid_argument("Microscope runtime tool is invalid");
    }
    if (!std::isfinite(configuration.slowMotionSpeed)
            || configuration.slowMotionSpeed < 0.0F) {
        throw std::invalid_argument(
                "Microscope runtime slow-motion speed is invalid");
    }
    if (!std::isfinite(configuration.incubateTargetUps)
            || configuration.incubateTargetUps < 1.0) {
        throw std::invalid_argument(
                "Microscope runtime incubate target UPS is invalid");
    }
}

cellularity::render::RenderFrameCaptureMetadata
makeCaptureMetadata(std::uint64_t worldEpoch, std::uint64_t completedUpdateId,
        std::int64_t environmentRevision,
        cellularity::render::RenderCaptureStage stage,
        const NativeViewport& viewport,
        std::uint64_t modeGeneration = 0) {
    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = worldEpoch;
    metadata.modeGeneration = modeGeneration;
    metadata.completedUpdateId = completedUpdateId;
    metadata.environmentRevision = environmentRevision;
    metadata.stage = stage;
    const auto visibleBounds =
            cellularity::app::toRenderBounds(viewport.viewBounds(0.0f));
    metadata.viewport =
            cellularity::app::snapshotViewport(viewport, visibleBounds);
    return metadata;
}

cellularity::render::RenderCaptureStage
renderCaptureStage(MicroscopeFrameController::CaptureStage stage) {
    switch (stage) {
        case MicroscopeFrameController::CaptureStage::StableWorld:
            return cellularity::render::RenderCaptureStage::StableWorld;
        case MicroscopeFrameController::CaptureStage::AfterUpdatePhaseOne:
            return cellularity::render::RenderCaptureStage::AfterUpdatePhaseOne;
        case MicroscopeFrameController::CaptureStage::AfterCompletedUpdate:
            return cellularity::render::RenderCaptureStage::AfterCompletedUpdate;
    }
    return cellularity::render::RenderCaptureStage::Unspecified;
}

} // namespace

NativeCellularityController::NativeCellularityController(
        bool isPreviewMode, const Environment& environment)
        : mPreviewMode(isPreviewMode), mSimulationOwner(isPreviewMode, environment),
          mInteraction(std::make_unique<NativeInteractionController>(
                  static_cast<float>(
                          mSimulationOwner.read().world().mEnvironment.geometry().radius() * 1.1),
                  mSimulationOwner.read().worldEpoch())),
          mRenderSession(std::make_unique<NativeRenderSession>(
                  mSimulationOwner.read().world().mEnvironment.maxCellCount)),
          mEnvironmentFieldPreviewEndpoint(
                  cellularity::app::createEnvironmentFieldPreviewEndpoint()),
          mGenomePreviewEndpoint(
                  cellularity::app::genome_preview::createGenomePreviewEndpoint(
                          mSimulationOwner.read().world().mEnvironment.maxCellCount)),
          mSubstratePreviewCaptureEndpoint(
                  cellularity::app::microscope::createSubstratePreviewCaptureEndpoint()) {
    mInteraction->setViewportCamera(0.0F, 0.0F,
            initialViewportZoom(mPreviewMode, environment.geometry().radius()));
}

NativeCellularityController::NativeCellularityController(
        std::unique_ptr<SimulationWorld> world)
        : mPreviewMode(world != nullptr && world->mEnvironment.mIsPreviewMode),
          mSimulationOwner(std::move(world)),
          mInteraction(std::make_unique<NativeInteractionController>(
                  static_cast<float>(
                          mSimulationOwner.read().world().mEnvironment.geometry().radius() * 1.1),
                  mSimulationOwner.read().worldEpoch())),
          mRenderSession(std::make_unique<NativeRenderSession>(
                  mSimulationOwner.read().world().mEnvironment.maxCellCount)),
          mEnvironmentFieldPreviewEndpoint(
                  cellularity::app::createEnvironmentFieldPreviewEndpoint()),
          mGenomePreviewEndpoint(
                  cellularity::app::genome_preview::createGenomePreviewEndpoint(
                          mSimulationOwner.read().world().mEnvironment.maxCellCount)),
          mSubstratePreviewCaptureEndpoint(
                  cellularity::app::microscope::createSubstratePreviewCaptureEndpoint()) {
    const auto access = mSimulationOwner.read();
    mInteraction->setViewportCamera(0.0F, 0.0F,
            initialViewportZoom(mPreviewMode,
                    access.world().mEnvironment.geometry().radius()));
}

NativeCellularityController::~NativeCellularityController() {
    stopMicroscopeRuntime();
    mSubstratePreviewCaptureEndpoint->close();
    mGenomePreviewEndpoint->close();
    mEnvironmentFieldPreviewEndpoint->close();
}

NativeSimulationOwner::ReadAccess
NativeCellularityController::readWorld() const {
    return mSimulationOwner.read();
}

NativeSimulationOwner::WriteAccess NativeCellularityController::writeWorld() {
    return mSimulationOwner.write();
}

NativeWorldContext NativeCellularityController::worldContext(SimulationWorld& world) {
    return NativeWorldContext{world, mInteraction->viewport()};
}

bool NativeCellularityController::isPreviewMode() const {
    return mPreviewMode;
}

bool NativeCellularityController::hasRenderSurfaceSession() const noexcept {
    return mRenderSession->hasSurfaceSession();
}

void NativeCellularityController::installRenderSurfaceSession(
        std::unique_ptr<cellularity::render::RenderSurfaceSession>
                surfaceSession) {
    mRenderSession->installSurfaceSession(std::move(surfaceSession));
    if (mInteraction->viewport().isReady()) {
        mRenderSession->resizeSurface(
                mInteraction->viewport().surfaceWidth(),
                mInteraction->viewport().surfaceHeight());
    }
}

void NativeCellularityController::installWorkerRenderSurfaceSession(
        std::unique_ptr<cellularity::render::RenderSurfaceSession>
                surfaceSession) {
    mRenderSession->installSurfaceSession(std::move(surfaceSession));
}

bool NativeCellularityController::uninstallRenderSurfaceSession(
        cellularity::render::RenderBackendKind backendKind,
        const void* bindingIdentity) noexcept {
    return mRenderSession->uninstallSurfaceSession(
            backendKind, bindingIdentity);
}

cellularity::render::RenderBackendKind
NativeCellularityController::renderBackendKind() const {
    return mRenderSession->backendKind();
}

bool NativeCellularityController::hasRendererResources() const {
    return mRenderSession->hasResources();
}

bool NativeCellularityController::hasRendererGpuBuffers() const {
    return mRenderSession->hasBackendGpuBuffers();
}

int NativeCellularityController::rendererVisibleCellCount() const {
    return mRenderSession->visibleCellCount();
}

std::array<std::uint64_t, 4>
NativeCellularityController::microscopePreparationFailureState()
        const noexcept {
    return mRenderSession->microscopePreparationFailureState();
}

void NativeCellularityController::setViewportSurfaceSize(int width,
        int height) {
    mInteraction->setViewportSurfaceSize(width, height);
    mRenderSession->resizeSurface(width, height);
}

void NativeCellularityController::setViewportPresentationSize(int width,
        int height) {
    mInteraction->setViewportSurfaceSize(width, height);
}

void NativeCellularityController::setViewportCamera(float cameraX,
        float cameraY, float zoom) {
    mInteraction->setViewportCamera(cameraX, cameraY, zoom);
}

void NativeCellularityController::moveViewportByScreenDelta(float dx,
        float dy) {
    mInteraction->moveViewportByScreenDelta(dx, dy);
}

void NativeCellularityController::zoomViewportAt(float factor, float screenX,
        float screenY) {
    mInteraction->zoomViewportAt(factor, screenX, screenY);
}

float NativeCellularityController::viewportScreenToWorldX(float screenX) const {
    return mInteraction->viewportScreenToWorldX(screenX);
}

float NativeCellularityController::viewportScreenToWorldY(float screenY) const {
    return mInteraction->viewportScreenToWorldY(screenY);
}

std::array<float, 7> NativeCellularityController::viewportState() const {
    return mInteraction->viewportState();
}

float NativeCellularityController::viewportCameraX() const {
    return mInteraction->viewportState()[0];
}

float NativeCellularityController::viewportCameraY() const {
    return mInteraction->viewportState()[1];
}

void NativeCellularityController::setGenomePreviewGridEnabled(bool enabled) {
    mGenomePreviewEndpoint->mFacade->setGridEnabled(enabled);
}

void NativeCellularityController::setGenomePreviewTreeEnabled(bool enabled) {
    auto access = writeWorld();
    NativeWorldContext context = worldContext(access.world());
    mGenomePreviewEndpoint->mFacade->setTreeEnabled(context, enabled);
    mInteraction->publishViewportIfChanged();
}

void NativeCellularityController::setGenomePreviewSelectedGeneIndex(
        int selectedGeneIndex) {
    mGenomePreviewEndpoint->mFacade->setSelectedGeneIndex(selectedGeneIndex);
}

void NativeCellularityController::resetGenomePreviewTreeState() {
    auto access = writeWorld();
    NativeWorldContext context = worldContext(access.world());
    mGenomePreviewEndpoint->mFacade->resetTreeState(context);
}

void NativeCellularityController::beginGenomePreviewTreeStep() {
    auto access = writeWorld();
    NativeWorldContext context = worldContext(access.world());
    mGenomePreviewEndpoint->mFacade->beginTreeStep(context);
}

void NativeCellularityController::recordGenomePreviewTreeSplits() {
    auto access = writeWorld();
    NativeWorldContext context = worldContext(access.world());
    mGenomePreviewEndpoint->mFacade->recordTreeSplits(context);
}

void NativeCellularityController::captureGenomePreviewGhost() {
    auto access = writeWorld();
    NativeWorldContext context = worldContext(access.world());
    auto metadata = makeCaptureMetadata(
            access.worldEpoch(), access.completedUpdateCount(),
            access.environmentRevision(),
            cellularity::render::RenderCaptureStage::GenomePreviewGhost,
            context.viewport);
    mGenomePreviewEndpoint->mFacade->captureGhost(context, metadata);
}

void NativeCellularityController::clearGenomePreviewGhost() {
    mGenomePreviewEndpoint->mFacade->clearGhost();
}

void NativeCellularityController::loadRendererResources(
        const NativeRenderAssets& assets,
        NativeRenderTextureLoader& textureLoader) {
    mRenderSession->loadResources(mPreviewMode, assets, textureLoader);
}

void NativeCellularityController::releaseRendererResources() {
    mRenderSession->releaseResources();
}

void NativeCellularityController::abandonRendererResources() {
    mRenderSession->abandonResources();
    mInteraction->abandonViewportSurface();
}

std::uint64_t NativeCellularityController::genomePreviewSurfaceCreated() noexcept {
    return mGenomePreviewEndpoint->surfaceCreated();
}

void NativeCellularityController::genomePreviewSurfaceDestroyed(
        std::uint64_t attachmentGeneration) noexcept {
    mGenomePreviewEndpoint->surfaceDestroyed(attachmentGeneration);
}

std::uint64_t NativeCellularityController::substratePreviewSurfaceCreated() noexcept {
    return mSubstratePreviewCaptureEndpoint->surfaceCreated();
}

void NativeCellularityController::substratePreviewSurfaceDestroyed(
        std::uint64_t attachmentGeneration) noexcept {
    mSubstratePreviewCaptureEndpoint->surfaceDestroyed(attachmentGeneration);
}

void NativeCellularityController::update(bool isForRendering, double speed) {
    auto access = writeWorld();
    access.update(isForRendering, speed);
}

void NativeCellularityController::update1(bool isForRendering, double speed) {
    auto access = writeWorld();
    access.update1(isForRendering, speed);
}

void NativeCellularityController::update2(bool forView, double speed) {
    auto access = writeWorld();
    access.update2(forView, speed);
}

std::uint64_t NativeCellularityController::completedUpdateCount() const {
    auto access = readWorld();
    return access.completedUpdateCount();
}

void NativeCellularityController::resetCompletedUpdateCount() {
    auto access = writeWorld();
    access.resetCompletedUpdateCount();
}

void NativeCellularityController::invalidateRenderWorldEpoch(
        std::uint64_t worldEpoch) {
    mInteraction->advanceCommandWorldEpoch(worldEpoch);
    mRenderSession->invalidateWorldEpoch(worldEpoch);
}

cellularity::render::RenderFrameResult
NativeCellularityController::drawGenomePreview() {
    return mGenomePreviewEndpoint->draw(*this);
}

std::vector<std::uint8_t>
NativeCellularityController::captureGenomeFilePreviewRgba(
        const cellularity::GenomeData& genome,
        double maximumSimulationTime) {
    return mGenomePreviewEndpoint->mFacade->captureFilePreviewRgba(
            genome, maximumSimulationTime, mRenderSession->surfaceSession());
}

cellularity::render::RenderFrameResult
NativeCellularityController::drawEnvironmentFieldPreview() {
    const std::shared_ptr<const cellularity::app::EnvironmentFieldPreviewState>
            state = mEnvironmentFieldPreviewEndpoint->retainedState();
    const std::optional<cellularity::render::EnvironmentFieldPreviewCommand>
            command = state != nullptr
            ? mRenderSession->prepareEnvironmentFieldPreview(
                      *state, mInteraction->viewport())
            : std::nullopt;
    return command.has_value()
            ? mRenderSession->drawEnvironmentFieldPreview(*command)
            : cellularity::render::makeRenderFrameResult(
                      cellularity::render::RenderFrameOutcome::SkippedNotReady);
}

std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
NativeCellularityController::environmentFieldPreviewEndpoint() const noexcept {
    return mEnvironmentFieldPreviewEndpoint;
}

std::shared_ptr<cellularity::app::genome_preview::GenomePreviewEndpoint>
NativeCellularityController::genomePreviewEndpoint() const noexcept {
    return mGenomePreviewEndpoint;
}

std::shared_ptr<cellularity::app::microscope::SubstratePreviewCaptureEndpoint>
NativeCellularityController::substratePreviewCaptureEndpoint() const noexcept {
    return mSubstratePreviewCaptureEndpoint;
}

int NativeCellularityController::selectGenomePreviewGeneAt(double worldX,
        double worldY) {
    auto access = readWorld();
    return mGenomePreviewEndpoint->mFacade->selectGeneAt(
            access.world(), worldX, worldY);
}

int NativeCellularityController::genomePreviewTreeNodeCount() const {
    return mGenomePreviewEndpoint->mFacade->treeNodeCount();
}

void NativeCellularityController::invalidateMicroscopeFrameData() {
    mInteraction->invalidateFrameData();
}

void NativeCellularityController::setMicroscopeTemperatureMode(
        int temperatureMode) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerTemperatureMode(temperatureMode);
        return;
    }
    auto access = writeWorld();
    auto& simulationState = access.microscopeSimulation();
    if (simulationState.setTemperatureMode(temperatureMode)) {
        mInteraction->onTemperatureModeChanged(temperatureMode,
                access.microscopeInteraction());
        mRenderSession->invalidateModeGeneration(simulationState.modeGeneration());
    }
}

void NativeCellularityController::setMicroscopeToolOrdinal(int toolOrdinal) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerToolOrdinal(toolOrdinal);
        return;
    }
    auto access = writeWorld();
    access.microscopeInteraction().setToolOrdinal(toolOrdinal);
    mInteraction->synchronizePresentationInteraction(
            access.microscopeInteraction());
}

void NativeCellularityController::setMicroscopeSlowMotionSpeed(
        float slowMotionSpeed) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerSlowMotionSpeed(slowMotionSpeed);
        return;
    }
    auto access = writeWorld();
    access.microscopeSimulation().setSlowMotionSpeed(slowMotionSpeed);
}

void NativeCellularityController::setMicroscopeIncubateTargetUps(
        double targetUps) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerOptionalTargetUps(targetUps);
        return;
    }
    auto access = writeWorld();
    access.microscopeSimulation().setLegacyIncubateTargetUps(targetUps);
}

void NativeCellularityController::setMicroscopePerformanceHudEnabled(
        bool enabled) noexcept {
    mPerformanceHudEnabled.store(enabled, std::memory_order_relaxed);
}

bool NativeCellularityController::microscopePerformanceHudEnabled()
        const noexcept {
    return mPerformanceHudEnabled.load(std::memory_order_relaxed);
}

void NativeCellularityController::startMicroscopeRuntime() {
    startMicroscopeRuntime(microscopeRuntimeStartConfiguration());
}

void NativeCellularityController::startMicroscopeRuntime(
        const cellularity::microscope::MicroscopeRuntimeStartConfiguration&
                configuration) {
    if (mPreviewMode || mMicroscopeRuntime != nullptr) {
        return;
    }
    validateRuntimeStartConfiguration(configuration);
    float cameraBoundsRadius = 1.0f;
    {
        auto access = writeWorld();
        cameraBoundsRadius =
                static_cast<float>(access.world().mEnvironment.geometry().radius() * 1.1);
        auto& simulationState = access.microscopeSimulation();
        simulationState.setTemperatureMode(configuration.temperatureMode);
        simulationState.setSlowMotionSpeed(configuration.slowMotionSpeed);
        simulationState.setLegacyIncubateTargetUps(
                configuration.incubateTargetUps);
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        simulationState.setSchedulerLifecyclePaused(
                configuration.lifecyclePaused,
                cellularity::simulation::simulationSchedulerNow());
#endif
        access.microscopeInteraction().setToolOrdinal(
                configuration.toolOrdinal);
        mInteraction->setPresentationTemperatureMode(
                configuration.temperatureMode);
        mInteraction->synchronizePresentationInteraction(
                access.microscopeInteraction());
    }
    mPerformanceHudEnabled.store(
            configuration.performanceHudEnabled, std::memory_order_relaxed);
    mMicroscopeRuntime =
            std::make_unique<cellularity::microscope::NativeMicroscopeRuntime>(
                    mSimulationOwner, *mInteraction, *mRenderSession, cameraBoundsRadius,
                    mPerformanceHudEnabled);
    mMicroscopeRuntime->start();
}

void NativeCellularityController::stopMicroscopeRuntime() noexcept {
    if (mMicroscopeRuntime != nullptr) {
        mMicroscopeRuntime->stop();
    }
}

bool NativeCellularityController::microscopeRuntimeActive() const noexcept {
    return mMicroscopeRuntime != nullptr;
}

void NativeCellularityController::setMicroscopeLifecyclePaused(bool paused) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerLifecycle(paused);
    }
}

cellularity::microscope::NativeMicroscopeRuntimeSnapshot
NativeCellularityController::microscopeRuntimeSnapshot() const {
    return mMicroscopeRuntime == nullptr
            ? cellularity::microscope::NativeMicroscopeRuntimeSnapshot{}
            : mMicroscopeRuntime->snapshot();
}

cellularity::microscope::MicroscopeRuntimeStartConfiguration
NativeCellularityController::microscopeRuntimeStartConfiguration() {
    if (mMicroscopeRuntime != nullptr) {
        auto request = std::make_shared<
                cellularity::microscope::NativeMicroscopeRequest>(
                cellularity::microscope::NativeMicroscopeRequestOperation::
                        InspectRuntimeConfiguration);
        executeMicroscopeOwnerRequest(
                cellularity::microscope::MicroscopeWorldCommandKind::Inspect,
                request, false);
        return request->runtimeConfiguration;
    }

    const auto access = readWorld();
    const auto& simulationState = access.microscopeSimulation();
    return {
            simulationState.temperatureMode(),
            access.microscopeInteraction().toolOrdinal(),
            simulationState.slowMotionSpeed(),
            simulationState.legacyIncubateTargetUps(),
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
            simulationState.schedulerLifecyclePaused(),
#else
            false,
#endif
            microscopePerformanceHudEnabled()
    };
}

void NativeCellularityController::executeMicroscopeOwnerRequest(
        cellularity::microscope::MicroscopeWorldCommandKind kind,
        const std::shared_ptr<cellularity::microscope::NativeMicroscopeRequest>& request,
        bool lifecycleBarrier) {
    if (mMicroscopeRuntime == nullptr || request == nullptr) {
        throw std::logic_error(
                "Microscope owner request requires an active runtime");
    }
    const std::uint64_t requestId =
            mNextMicroscopeRequestId.fetch_add(1, std::memory_order_relaxed);
    if (requestId == 0 || requestId == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Microscope owner request id exhausted");
    }
    auto completion =
            std::make_shared<cellularity::microscope::MicroscopeCommandCompletion>();
    const auto enqueueResult =
            lifecycleBarrier ? mInteraction->enqueueOwnerBarrier(kind, requestId,
                                       completion, request)
                             : mInteraction->enqueueOwnerRequest(kind, requestId,
                                       completion, request);
    if (!enqueueResult.accepted()) {
        throw std::runtime_error("Microscope owner request queue is busy");
    }

    cellularity::microscope::MicroscopeCommandCompletionSnapshot resolved;
    while (
            completion->waitUntil(cellularity::microscope::microscopeCommandNow() + std::chrono::seconds(1),
                    resolved)
            == cellularity::microscope::MicroscopeCommandWaitResult::DeadlineReached) {
        if (mMicroscopeRuntime->snapshot().failed) {
            throw std::runtime_error("Microscope owner runtime failed");
        }
    }
    if (request->failure != nullptr) {
        std::rethrow_exception(request->failure);
    }
    if (resolved.resolution != cellularity::microscope::MicroscopeCommandResolution::Applied) {
        throw std::runtime_error("Microscope owner rejected the request");
    }
}

void NativeCellularityController::resetMicroscopeWorld() {
    auto request = std::make_shared<
            cellularity::microscope::NativeMicroscopeRequest>(
            cellularity::microscope::NativeMicroscopeRequestOperation::ResetWorld);
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Reset, request,
            true);
}

std::uint64_t
NativeCellularityController::writeMicroscopeSubstratePath(
        const std::string& path) {
    auto request = std::make_shared<
            cellularity::microscope::NativeMicroscopeRequest>(
            cellularity::microscope::NativeMicroscopeRequestOperation::SaveSubstrate);
    request->path = path;
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Save, request,
            false);
    return request->outputToken;
}

std::uint64_t NativeCellularityController::writeSubstratePathAndPreparePreview(
        const std::string& path) {
    if (mMicroscopeRuntime != nullptr) {
        return writeMicroscopeSubstratePath(path);
    }

    auto access = readWorld();
    writeSubstratePath(path, access.world());
    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = access.worldEpoch();
    metadata.completedUpdateId = access.completedUpdateCount();
    metadata.environmentRevision = access.environmentRevision();
    return mRenderSession->tryPrepareSubstratePreviewFrame(access.world(),
            metadata);
}

std::vector<std::uint8_t>
NativeCellularityController::captureSubstratePreviewRgba(
        std::uint64_t token) {
    return mRenderSession->captureSubstratePreviewRgba(
            token, mInteraction->nextBackgroundColor());
}

void NativeCellularityController::discardSubstratePreviewFrame(
        std::uint64_t token) noexcept {
    mRenderSession->discardSubstratePreviewFrame(token);
}

std::vector<std::uint8_t>
NativeCellularityController::applyMicroscopeEnvironmentPatch(
        std::vector<std::uint8_t> patchBytes) {
    auto request =
            std::make_shared<cellularity::microscope::NativeMicroscopeRequest>(
                    cellularity::microscope::NativeMicroscopeRequestOperation::
                            ApplyEnvironmentPatch);
    request->inputBytes = std::move(patchBytes);
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Apply, request,
            false);
    return std::move(request->outputBytes);
}

std::optional<std::vector<std::uint8_t>>
NativeCellularityController::inspectMicroscopeCell(int cellId) {
    auto request = std::make_shared<
            cellularity::microscope::NativeMicroscopeRequest>(
            cellularity::microscope::NativeMicroscopeRequestOperation::InspectCell);
    request->cellId = cellId;
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Inspect, request,
            false);
    if (!request->hasOutput) {
        return std::nullopt;
    }
    return std::move(request->outputBytes);
}

std::vector<std::uint8_t>
NativeCellularityController::inspectMicroscopeEnvironment() {
    auto request =
            std::make_shared<cellularity::microscope::NativeMicroscopeRequest>(
                    cellularity::microscope::NativeMicroscopeRequestOperation::
                            InspectEnvironment);
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Inspect, request,
            false);
    return std::move(request->outputBytes);
}

cellularity::microscope::NativeMicroscopeRuntimeSnapshot
NativeCellularityController::inspectMicroscopeStats() {
    auto request = std::make_shared<
            cellularity::microscope::NativeMicroscopeRequest>(
            cellularity::microscope::NativeMicroscopeRequestOperation::InspectStats);
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Inspect, request,
            false);
    cellularity::microscope::NativeMicroscopeRuntimeSnapshot result;
    result.completedUpdateCount = request->completedUpdateCount;
    result.cellCount = request->cellCount;
    result.foodMass = request->foodMass;
    result.simulationTime = request->simulationTime;
    return result;
}

std::optional<std::vector<std::uint8_t>>
NativeCellularityController::inspectMicroscopeSelectedGenome() {
    auto request =
            std::make_shared<cellularity::microscope::NativeMicroscopeRequest>(
                    cellularity::microscope::NativeMicroscopeRequestOperation::
                            InspectSelectedGenome);
    executeMicroscopeOwnerRequest(
            cellularity::microscope::MicroscopeWorldCommandKind::Inspect, request,
            false);
    if (!request->hasOutput) {
        return std::nullopt;
    }
    return std::move(request->outputBytes);
}

bool NativeCellularityController::stepMicroscopeFrame() {
    if (mMicroscopeRuntime != nullptr) {
        return false;
    }
    if (mRenderSession->consumeUrgentMicroscopeCaptureRequest() || mInteraction->consumeViewportCaptureHint()) {
        mInteraction->invalidateFrameData();
    }
    auto access = writeWorld();
    NativeWorldContext context = worldContext(access.world());
    const std::uint64_t worldEpoch = access.worldEpoch();
    const std::int64_t environmentRevision = access.environmentRevision();
    auto& simulationState = access.microscopeSimulation();
    return mInteraction->stepFrame(
            context, access.simulationStepper(), simulationState,
            access.microscopeInteraction(),
            [this, &access, &simulationState, worldEpoch, environmentRevision](
                    const SimulationWorld& world, const NativeViewport& viewport, float margin,
                    MicroscopeFrameController::CaptureStage stage) {
                auto metadata =
                        makeCaptureMetadata(worldEpoch, access.completedUpdateCount(),
                                environmentRevision, renderCaptureStage(stage),
                                viewport, simulationState.modeGeneration());
                const auto inputState = mInteraction->resolvedInputState();
                metadata.resolvedInput.lastResolvedInputSequence =
                        inputState.lastResolvedInputSequence;
                metadata.resolvedInput.appliedWorldRevision =
                        inputState.appliedWorldRevision;
                access.world().ensureFoodSpatialIndexForCapture();
                mRenderSession->captureMicroscopeFrame(world, viewport, margin,
                        metadata);
            });
}

bool NativeCellularityController::stepMicroscopeViewportInertia() {
    return mInteraction->stepViewportInertia();
}

void NativeCellularityController::setMicroscopeViewportFlingVelocity(
        float velocityX, float velocityY) {
    mInteraction->setViewportFlingVelocity(velocityX, velocityY);
}

void NativeCellularityController::clearMicroscopeInputQueues() {
    auto access = writeWorld();
    mInteraction->clearInputQueues(access.microscopeInteraction());
}

void NativeCellularityController::queueMicroscopeTouchDown(int pointerId,
        float screenX,
        float screenY) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerTouchDown(pointerId, screenX, screenY);
        return;
    }
    mInteraction->queueTouchDown(pointerId, screenX, screenY);
}

void NativeCellularityController::queueMicroscopeTouchMove(int pointerId,
        float screenX,
        float screenY) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerTouchMove(pointerId, screenX, screenY,
                false);
        return;
    }
    mInteraction->queueTouchMove(pointerId, screenX, screenY);
}

void NativeCellularityController::queueMicroscopeTouchUp(int pointerId) {
    if (mMicroscopeRuntime != nullptr) {
        (void)mInteraction->enqueueOwnerTouchUp(pointerId);
        return;
    }
    mInteraction->queueTouchUp(pointerId);
}

void NativeCellularityController::queueMicroscopeTap(
        float screenX, float screenY,
        const cellularity::GenomeData* genome) {
    if (mMicroscopeRuntime != nullptr) {
        std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                commandGenome;
        if (genome != nullptr) {
            commandGenome =
                    std::make_shared<const cellularity::microscope::MicroscopeCommandGenome>(
                            *genome);
        }
        (void)mInteraction->enqueueOwnerTap(screenX, screenY,
                std::move(commandGenome));
        return;
    }
    mInteraction->queueTap(screenX, screenY, genome);
}

bool NativeCellularityController::queueMicroscopePan(float dx, float dy) {
    return mInteraction->queuePan(dx, dy);
}

bool NativeCellularityController::queueMicroscopeZoom(float factor,
        float screenX,
        float screenY) {
    return mInteraction->queueZoom(factor, screenX, screenY);
}

bool NativeCellularityController::queueMicroscopeFling(float velocityX,
        float velocityY) {
    return mInteraction->queueFling(velocityX, velocityY);
}

void NativeCellularityController::submitMicroscopeInputBatch(
        const cellularity::microscope::MicroscopeInputBatch& batch,
        std::uint64_t authoritativeInputEpoch,
        std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                tapGenome,
        cellularity::microscope::MicroscopeInputBatchResult& result,
        bool rejectTapForStalePlacementGenome) {
    result.batchSequence = batch.sequence;
    result.inputEpoch = authoritativeInputEpoch;
    if (mMicroscopeRuntime == nullptr || authoritativeInputEpoch == 0) {
        result.ingressStatus = cellularity::microscope::
                MicroscopeInputIngressStatus::AttachmentUnavailable;
        result.commandCount = 0;
        return;
    }
    if (batch.expectedInputEpoch != authoritativeInputEpoch) {
        result.ingressStatus = cellularity::microscope::
                MicroscopeInputIngressStatus::StaleInputEpoch;
        result.commandCount = 0;
        return;
    }
    mInteraction->submitInputBatch(batch, std::move(tapGenome), result,
            rejectTapForStalePlacementGenome);
}

cellularity::microscope::PresentationMotionSnapshot
NativeCellularityController::microscopePresentationMotionSnapshot() const noexcept {
    return mInteraction->presentationMotionSnapshot();
}

bool NativeCellularityController::requestMicroscopeInputResync() noexcept {
    return mInteraction->requestInputResync();
}

std::uint32_t
NativeCellularityController::microscopeActivePointerGenerationCount() const noexcept {
    return mInteraction->activePointerGenerationCount();
}

void NativeCellularityController::closeMicroscopeInputAdmission() noexcept {
    mInteraction->closeInputAdmission();
}

cellularity::render::RenderFrameResult
NativeCellularityController::drawMicroscope() {
    mSubstratePreviewCaptureEndpoint->applyPendingOnRenderOwner(*this);
    if (mMicroscopeRuntime != nullptr) {
        cellularity::microscope::PresentationViewportSnapshot newest;
        if (mInteraction->tryConsumeNewestPresentationViewport(newest) && (!mHasPresentedViewportSnapshot || newest.viewport.revision >= mPresentedViewportSnapshot.viewport.revision)) {
            mPresentedViewportSnapshot = newest;
            mHasPresentedViewportSnapshot = true;
        }
        if (!mHasPresentedViewportSnapshot) {
            return cellularity::render::makeRenderFrameResult(
                    cellularity::render::RenderFrameOutcome::SkippedNotReady);
        }
        cellularity::render::RenderPresentationState presentation;
        presentation.viewport = mPresentedViewportSnapshot.viewport;
        presentation.backgroundColor = mInteraction->nextBackgroundColor();
#ifdef CELLULARITY_PERFORMANCE_HUD
        cellularity::render::RenderFrameResult result;
        if (mPerformanceHudEnabled.load(std::memory_order_relaxed)) {
            const auto runtimeSnapshot = mMicroscopeRuntime->snapshot();
            const cellularity::microscope::MicroscopeDebugHudTelemetry debugTelemetry{
                    runtimeSnapshot.rateTelemetry.actualRateSampleSequence,
                    runtimeSnapshot.rateTelemetry.actualRateValid,
                    runtimeSnapshot.rateTelemetry.actualCompletedUps,
                    runtimeSnapshot.stageTimings
            };
            result = mRenderSession->drawMicroscope(presentation,
                    &debugTelemetry);
        } else {
            result =
                    mRenderSession->drawMicroscope(presentation);
        }
#else
        const cellularity::render::RenderFrameResult result =
                mRenderSession->drawMicroscope(presentation);
#endif
        if (cellularity::render::isPrimaryContentPresented(result)) {
            markMicroscopeFramePresented();
        }
        return result;
    }
    // This is the pre-existing frame-rate wait, moved ahead of exchange polling
    // so ordinary worker preparation overlaps it instead of missing a complete
    // display interval. It is not a wait on the worker.
    mInteraction->paceBeforeDraw();
    const cellularity::render::RenderFrameResult result =
            mRenderSession->drawMicroscope(
                    mInteraction->viewport(),
                    mInteraction->nextBackgroundColor());
    if (cellularity::render::isPrimaryContentPresented(result)) {
        mInteraction->markDrawFrameConsumed();
    }
    return result;
}

cellularity::render::RenderFrameResult
NativeCellularityController::clearMicroscopeNoRender() {
    mSubstratePreviewCaptureEndpoint->applyPendingOnRenderOwner(*this);
    if (mMicroscopeRuntime != nullptr) {
        cellularity::microscope::PresentationViewportSnapshot newest;
        if (mInteraction->tryConsumeNewestPresentationViewport(newest) && (!mHasPresentedViewportSnapshot || newest.viewport.revision >= mPresentedViewportSnapshot.viewport.revision)) {
            mPresentedViewportSnapshot = newest;
            mHasPresentedViewportSnapshot = true;
        }
        if (mHasPresentedViewportSnapshot) {
            return mRenderSession->clearMicroscopeNoRender(
                    mPresentedViewportSnapshot.viewport);
        }
        return cellularity::render::makeRenderFrameResult(
                cellularity::render::RenderFrameOutcome::SkippedNotReady);
    }
    return mRenderSession->clearMicroscopeNoRender(
            mInteraction->viewport());
}

cellularity::render::RenderFrameResult
NativeCellularityController::drawMicroscopeForCurrentMode() {
    return mInteraction->presentationTemperatureMode()
                    == MicroscopeFrameController::MODE_INCUBATE_NO_RENDER
            ? clearMicroscopeNoRender()
            : drawMicroscope();
}

void NativeCellularityController::markMicroscopeFramePresented() {
    const auto* frame = mRenderSession->preparedFrame();
    if (frame == nullptr || !frame->valid) {
        return;
    }
    mInteraction->markFramePresented(
            frame->resolvedInput.lastResolvedInputSequence,
            frame->resolvedInput.appliedWorldRevision,
            frame->timeline.sourceCapturedNanos, frame->timeline.framePreparedNanos,
            mMicroscopeRuntime != nullptr && mHasPresentedViewportSnapshot
                    ? mPresentedViewportSnapshot.viewport.revision
                    : mInteraction->viewport().revision(),
            cellularity::render::renderTimelineNowNanos());
    mInteraction->commandMailbox().markPresented(
            frame->identity.worldEpoch,
            frame->resolvedInput.lastResolvedInputSequence,
            frame->resolvedInput.appliedWorldRevision,
            cellularity::microscope::microscopeCommandNow());
}

void NativeCellularityController::resetMicroscopeInputTimelineMeasurements() {
    mInteraction->resetInputTimelineMeasurements();
}

cellularity::microscope::InputTimelineSnapshot
NativeCellularityController::microscopeInputTimelineSnapshot() const {
    return mInteraction->inputTimelineSnapshot();
}
