#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/NativeCellularityController.h"
#include "app/NativeRenderSession.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "app/interaction/NativeViewport.h"
#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "formats/GenomeDraftCodec.h"
#include "render/NativeRenderAssets.h"
#include "render/NativeRenderTextureLoader.h"
#include "tests/support/FakeRenderSurfaceSession.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::render::OffscreenRenderTarget;
using cellularity::tests::assertTrue;
using cellularity::tests::FakeRenderSurfaceSession;

class EmptyRenderAssets final : public NativeRenderAssets {
public:
    std::string readText(const std::string&) const override {
        return {};
    }

    std::vector<unsigned char> readBytes(const std::string&) const override {
        return {};
    }
};

class EmptyTextureLoader final : public NativeRenderTextureLoader {
public:
    NativeRenderTexturePayload loadTexture(
            const std::string&, bool clampToEdge) override {
        NativeRenderTexturePayload payload;
        payload.width = 1;
        payload.height = 1;
        payload.wrapS = clampToEdge
                ? NativeRenderTextureWrap::ClampToEdge
                : NativeRenderTextureWrap::Repeat;
        payload.wrapT = payload.wrapS;
        payload.pixels = {255, 255, 255, 255};
        return payload;
    }
};

std::unique_ptr<SimulationWorld> createWorld() {
    Environment environment =
            Environment::constructDefaultEnvironment(8, 4, 1.0);
    auto world = std::make_unique<SimulationWorld>(false, environment);
    const int cell = cellularity::tests::addAliveCell(
            world->mCellData, CellType::PHAGOCYTE);
    world->mCellData.mPositionX[cell] = 0.0;
    world->mCellData.mPositionY[cell] = 0.0;
    world->mCellData.mRadius[cell] = 0.02;
    world->mCellData.refreshRadiusSq(cell);
    world->updateGrids();
    return world;
}

bool drawUntilReady(NativeRenderSession& session,
        const NativeViewport& viewport) {
    for (int attempt = 0; attempt < 10000; ++attempt) {
        if (cellularity::render::isPrimaryContentPresented(
                    session.drawMicroscope(
                            viewport, {0.2f, 0.3f, 0.4f}))) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

void testBackendLifecycleAndRetainedFrameOrchestration() {
    auto world = createWorld();
    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    NativeRenderSession session(
            world->mEnvironment.maxCellCount, std::move(backend));
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 1.0f);
    EmptyRenderAssets assets;
    EmptyTextureLoader textures;

    assertTrue(session.backendKind()
                    == cellularity::render::RenderBackendKind::OpenGlEs,
            "Render session exposes the selected high-level backend kind");
    session.loadResources(*world, assets, textures);
    session.resizeSurface(320, 240);
    assertTrue(fake->attachCount == 1 && fake->resizeCount == 1
                    && session.hasResources()
                    && session.hasBackendGpuBuffers(),
            "Render session delegates surface attach and resize lifecycle");

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 7;
    metadata.modeGeneration = 3;
    metadata.stage = cellularity::render::RenderCaptureStage::StableWorld;
    session.captureMicroscopeFrame(*world, viewport, 0.1f, metadata);
    assertTrue(drawUntilReady(session, viewport),
            "Prepared frame reaches a fake backend without any graphics API");
    const auto* retained = session.preparedFrame();
    assertTrue(retained != nullptr && fake->lastPresentedFrame == retained,
            "High-level backend receives the immutable published frame");

    session.abandonResources();
    assertTrue(fake->abandonCount == 1 && !session.hasResources()
                    && !session.hasBackendGpuBuffers(),
            "Surface loss abandons only backend-owned resources");
    session.loadResources(*world, assets, textures);
    assertTrue(cellularity::render::isPrimaryContentPresented(
                       session.drawMicroscope(
                               viewport, {0.4f, 0.5f, 0.6f}))
                    && fake->lastPresentedFrame == retained,
            "Backend recreation presents the retained CPU frame");
    session.releaseResources();
    assertTrue(fake->detachCount == 1 && !session.hasResources(),
            "Explicit detach is delegated through the same contract");
}

void testFirstFrameClearAndSubstrateCaptureOrchestration() {
    auto world = createWorld();
    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    NativeRenderSession session(
            world->mEnvironment.maxCellCount, std::move(backend));
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 1.0f);

    const auto firstFrame =
            session.drawMicroscope(viewport, {0.2f, 0.3f, 0.4f});
    assertTrue(firstFrame.outcome
                            == cellularity::render::RenderFrameOutcome::Presented
                    && firstFrame.flags
                            == cellularity::render::kFallbackClearPresented
                    && fake->clearCount == 1
                    && fake->lastClearColor
                            == std::array<float, 4>{0.31372549019f,
                                    0.31372549019f, 0.31372549019f, 1.0f},
            "Missing first frame delegates one defined clear presentation");

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 11;
    const std::uint64_t token =
            session.prepareSubstratePreviewFrame(*world, metadata);
    const std::vector<std::uint8_t> rgba =
            session.captureSubstratePreviewRgba(
                    token, {0.7f, 0.6f, 0.5f});
    assertTrue(rgba == fake->offscreenPixels && fake->offscreenBeginCount == 1
                    && fake->offscreenFinishCount == 1
                    && fake->lastOffscreenTarget
                            == OffscreenRenderTarget::SubstratePreview
                    && fake->presentCount == 1,
            "Substrate capture composes offscreen lifecycle with production presentation");
    assertTrue(session.captureSubstratePreviewRgba(
                              token, {0.7f, 0.6f, 0.5f})
                       .empty(),
            "Backend-neutral preview token remains one-shot");

    const std::uint64_t failedToken =
            session.prepareSubstratePreviewFrame(*world, metadata);
    fake->offscreenPassResult =
            cellularity::render::RenderPassStatus::Failed;
    bool threw = false;
    try {
        (void)session.captureSubstratePreviewRgba(
                failedToken, {0.7f, 0.6f, 0.5f});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assertTrue(threw && fake->offscreenCancelCount == 1,
            "A failed offscreen pass cancels capture without producing a screen outcome");
}

void testFrameOutcomePropagationAndEarlySkip() {
    using cellularity::render::kPrimaryContentPresented;
    using cellularity::render::RenderFrameOutcome;

    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    NativeRenderSession session(8, std::move(backend));
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 1.0f);

    constexpr std::array outcomes{
            RenderFrameOutcome::Presented,
            RenderFrameOutcome::Deferred,
            RenderFrameOutcome::SwapchainRecreated,
            RenderFrameOutcome::SurfaceLost,
            RenderFrameOutcome::SkippedNotReady,
            RenderFrameOutcome::Failed,
    };
    for (const RenderFrameOutcome outcome : outcomes) {
        fake->frameResult = cellularity::render::makeRenderFrameResult(
                outcome,
                outcome == RenderFrameOutcome::Presented
                        ? kPrimaryContentPresented
                        : 0);
        const auto clearResult = session.clearMicroscopeNoRender(viewport);
        assertTrue(clearResult.outcome == outcome
                        && clearResult.flags
                                == (outcome == RenderFrameOutcome::Presented
                                                ? kPrimaryContentPresented
                                                : 0)
                        && clearResult.semanticValue == 0,
                "No Render clear preserves each backend outcome directly");

        cellularity::render::EnvironmentFieldPreviewCommand command;
        const auto fieldResult = session.drawEnvironmentFieldPreview(command);
        assertTrue(fieldResult.outcome == outcome
                        && fieldResult.flags
                                == (outcome == RenderFrameOutcome::Presented
                                                ? kPrimaryContentPresented
                                                : 0)
                        && fieldResult.semanticValue == 0,
                "Environment preview preserves each backend outcome directly");
    }

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Presented, kPrimaryContentPresented);
    (void)session.clearMicroscopeNoRender(viewport);
    NativeViewport notReady(1.1f);
    const auto skipped =
            session.drawMicroscope(notReady, {0.2f, 0.3f, 0.4f});
    assertTrue(skipped.outcome == RenderFrameOutcome::SkippedNotReady
                    && skipped.flags == 0,
            "An early not-ready return cannot reuse a previous Presented result");

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Presented,
            cellularity::render::kFallbackClearPresented);
    bool rejectedWrongContent = false;
    try {
        cellularity::render::EnvironmentFieldPreviewCommand command;
        (void)session.drawEnvironmentFieldPreview(command);
    } catch (const std::logic_error&) {
        rejectedWrongContent = true;
    }
    assertTrue(rejectedWrongContent,
            "A valid wire value with the wrong transaction content is a terminal invariant failure");
}

void testExternalBackendUninstallRejectsStaleIdentityAndAttachedState() {
    int firstIdentity = 0;
    int staleIdentity = 0;
    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    fake->backendKind = cellularity::render::RenderBackendKind::Vulkan;
    fake->identity = &firstIdentity;
    NativeRenderSession session(8);
    session.installSurfaceSession(std::move(backend));

    assertTrue(!session.uninstallSurfaceSession(
                       cellularity::render::RenderBackendKind::Vulkan,
                       &staleIdentity)
                    && session.hasSurfaceSession(),
            "A stale Android surface callback cannot remove the current backend");

    fake->resources = true;
    assertTrue(!session.uninstallSurfaceSession(
                       cellularity::render::RenderBackendKind::Vulkan,
                       &firstIdentity)
                    && session.hasSurfaceSession(),
            "An attached external backend cannot be uninstalled");

    fake->resources = false;
    assertTrue(session.uninstallSurfaceSession(
                       cellularity::render::RenderBackendKind::Vulkan,
                       &firstIdentity)
                    && !session.hasSurfaceSession(),
            "The matching detached external backend can be removed exactly once");
}

void testGenomePreviewFrameIsAtomicAcrossModesAndCancelsOnFailure() {
    Environment environment =
            Environment::constructDefaultEnvironment(8, 0, 1.0);
    NativeCellularityController controller(true, environment);
    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    fake->backendKind = cellularity::render::RenderBackendKind::Vulkan;
    controller.installRenderSurfaceSession(std::move(backend));
    controller.setViewportSurfaceSize(320, 240);
    {
        auto access = controller.writeWorld();
        CellData& cells = access.world().mCellData;
        const int cell = cellularity::tests::addAliveCell(
                cells, CellType::PHAGOCYTE);
        cells.mPositionX[cell] = -0.02;
        cells.mPositionY[cell] = 0.0;
        cells.mRadius[cell] = 0.02;
        cells.refreshRadiusSq(cell);
        cells.setActiveGeneIndex(cell, cellularity::GeneIndex(0));
        access.world().updateGrids();
    }
    EmptyRenderAssets assets;
    EmptyTextureLoader textures;
    controller.loadRendererResources(assets, textures);
    controller.setGenomePreviewSelectedGeneIndex(0);

    const auto ordinaryResult = controller.drawGenomePreview();
    assertTrue(ordinaryResult.semanticValue == 1
                    && cellularity::render::isPrimaryContentPresented(
                            ordinaryResult)
                    && fake->genomeFrameCount == 1
                    && fake->genomeGridCount == 1
                    && fake->genomeDrawCount == 1
                    && fake->genomeEndCount == 1
                    && fake->genomeCancelCount == 0,
            "Ordinary Genome Preview closes one logical frame");

    controller.captureGenomePreviewGhost();
    {
        auto access = controller.writeWorld();
        access.world().mCellData.mPositionX[0] = 0.02;
        access.world().updateGrids();
    }
    const auto ghostResult = controller.drawGenomePreview();
    assertTrue(ghostResult.semanticValue == 1
                    && cellularity::render::isPrimaryContentPresented(
                            ghostResult)
                    && fake->genomeFrameCount == 2
                    && fake->genomeGridCount == 2
                    && fake->genomeDrawCount == 3
                    && fake->genomeEndCount == 2,
            "Ghost and live geometry share one completed logical frame");

    controller.clearGenomePreviewGhost();
    controller.resetGenomePreviewTreeState();
    controller.setGenomePreviewTreeEnabled(true);
    const auto treeResult = controller.drawGenomePreview();
    assertTrue(treeResult.semanticValue == 1
                    && cellularity::render::isPrimaryContentPresented(
                            treeResult)
                    && fake->genomeFrameCount == 3
                    && fake->genomeGridCount == 3
                    && fake->genomeTreeLineCount == 1
                    && fake->genomeDrawCount == 4
                    && fake->genomeEndCount == 3
                    && fake->attachCount == 1,
            "Tree and mode transitions retain one backend surface session");

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            cellularity::render::RenderFrameOutcome::Deferred);
    const auto deferredResult = controller.drawGenomePreview();
    assertTrue(deferredResult.outcome
                            == cellularity::render::RenderFrameOutcome::Deferred
                    && deferredResult.flags == 0
                    && deferredResult.semanticValue == 1
                    && fake->genomeEndCount == 4,
            "Genome Preview retains its visible count for a transient surface outcome");
    fake->frameResult = cellularity::render::makeRenderFrameResult(
            cellularity::render::RenderFrameOutcome::Presented,
            cellularity::render::kPrimaryContentPresented);

    controller.setGenomePreviewTreeEnabled(false);
    fake->throwOnGenomeDraw = true;
    bool threw = false;
    try {
        (void)controller.drawGenomePreview();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assertTrue(threw && fake->genomeFrameCount == 5
                    && fake->genomeEndCount == 4
                    && fake->genomeCancelCount == 1,
            "A failed Preview layer cancels rather than presents its frame");

    fake->throwOnGenomeDraw = false;
    controller.releaseRendererResources();
}

void testGenomePreviewAcknowledgesOnlyPresentedCurrentState() {
    using cellularity::app::genome_preview::GenomePreviewInteractionInput;
    using cellularity::app::genome_preview::GenomePreviewRestoreViewport;
    using cellularity::render::RenderFrameOutcome;

    Environment environment =
            Environment::setupPreviewSimulationEnvironment(16);
    NativeCellularityController controller(true, environment);
    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    fake->backendKind = cellularity::render::RenderBackendKind::Vulkan;
    controller.installRenderSurfaceSession(std::move(backend));
    controller.setViewportSurfaceSize(320, 240);
    EmptyRenderAssets assets;
    EmptyTextureLoader textures;
    controller.loadRendererResources(assets, textures);

    const auto endpoint = controller.genomePreviewEndpoint();
    const std::uint64_t attachment = endpoint->surfaceCreated();
    const std::vector<std::uint8_t> genome = writeGenomeDraft(
            cellularity::tests::canonicalDefaultGenomeData());
    const auto content = endpoint->publishContent(
            genome.data(), genome.size(), 0.0, 1, endpoint->generation());
    const auto view = endpoint->publishViewState(
            {0, true, false, 320, 240}, 1, endpoint->generation());
    const auto interaction = endpoint->enqueueInteraction(
            GenomePreviewInteractionInput{
                    1,
                    endpoint->generation(),
                    attachment,
                    GenomePreviewRestoreViewport{0.25F, -0.5F, 6.0F},
            },
            endpoint->generation());

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Deferred);
    (void)endpoint->draw(controller);
    const auto deferred = endpoint->readAcknowledgment();
    assertTrue(content.revision == 1 && view.revision == 1
                    && interaction.accepted()
                    && deferred.appliedContentRevision == 1
                    && deferred.appliedViewRevision == 1
                    && deferred.presentedContentRevision == 0
                    && deferred.presentedViewRevision == 0
                    && deferred.presentedInteractionToken == 0,
            "Deferred Genome Preview frames never acknowledge visible state");

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::SwapchainRecreated);
    (void)endpoint->draw(controller);
    const auto recreated = endpoint->readAcknowledgment();
    assertTrue(recreated.presentedContentRevision == 0
                    && recreated.presentedViewRevision == 0
                    && recreated.presentedInteractionToken == 0,
            "Swapchain recreation never acknowledges visible state");

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Presented,
            cellularity::render::kPrimaryContentPresented);
    (void)endpoint->draw(controller);
    const auto presented = endpoint->readAcknowledgment();
    assertTrue(presented.presentedContentRevision == 1
                    && presented.presentedViewRevision == 1
                    && presented.presentedInteractionToken == 1,
            "A primary presentation publishes one atomic current-state tuple");

    const auto nextView = endpoint->publishViewState(
            {0, false, true, 320, 240}, 2, endpoint->generation());
    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Deferred);
    (void)endpoint->draw(controller);
    const auto stalePresentation = endpoint->readAcknowledgment();
    assertTrue(nextView.revision == 2
                    && stalePresentation.appliedViewRevision == 2
                    && stalePresentation.presentedContentRevision == 1
                    && stalePresentation.presentedViewRevision == 1
                    && stalePresentation.presentedInteractionToken == 1,
            "Applied state can advance while the visible tuple stays unchanged");

    const auto capture = endpoint->enqueueFileCapture(
            7,
            genome.data(),
            genome.size(),
            0.0,
            endpoint->generation(),
            attachment);
    endpoint->applyPendingOnRenderOwner(controller);
    const auto afterCapture = endpoint->readAcknowledgment();
    assertTrue(capture.accepted()
                    && afterCapture.presentedContentRevision == 1
                    && afterCapture.presentedViewRevision == 1
                    && afterCapture.presentedInteractionToken == 1,
            "Offscreen capture never advances onscreen presentation state");

    fake->frameResult = cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Presented,
            cellularity::render::kPrimaryContentPresented);
    (void)endpoint->draw(controller);
    const auto newestPresentation = endpoint->readAcknowledgment();
    assertTrue(newestPresentation.presentedContentRevision == 1
                    && newestPresentation.presentedViewRevision == 2
                    && newestPresentation.presentedInteractionToken == 1,
            "The next primary presentation atomically advances to newest state");

    endpoint->surfaceDestroyed(attachment);
    const auto detached = endpoint->readAcknowledgment();
    assertTrue(detached.attachmentGeneration == 0
                    && detached.appliedContentRevision == 1
                    && detached.appliedViewRevision == 2
                    && detached.presentedContentRevision == 0
                    && detached.presentedViewRevision == 0
                    && detached.presentedInteractionToken == 0,
            "Surface loss invalidates visible state without discarding retained state");
    controller.releaseRendererResources();
}

} // namespace

int main() {
    testBackendLifecycleAndRetainedFrameOrchestration();
    testFirstFrameClearAndSubstrateCaptureOrchestration();
    testFrameOutcomePropagationAndEarlySkip();
    testExternalBackendUninstallRejectsStaleIdentityAndAttachedState();
    testGenomePreviewFrameIsAtomicAcrossModesAndCancelsOnFailure();
    testGenomePreviewAcknowledgesOnlyPresentedCurrentState();
    std::cout << "Render surface session tests passed\n";
    return 0;
}
