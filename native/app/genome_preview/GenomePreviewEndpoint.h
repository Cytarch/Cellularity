#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>

#include "app/genome_preview/GenomePreviewCaptureRequestStore.h"
#include "app/genome_preview/GenomePreviewInteractionQueue.h"
#include "app/genome_preview/GenomePreviewRetainedState.h"
#include "render/backend/RenderFrameResult.h"

class NativeCellularityController;
class NativeGenomePreviewFacade;

namespace cellularity::app::genome_preview {

struct GenomePreviewAcknowledgment final {
    std::uint64_t endpointGeneration = 0;
    std::uint64_t attachmentGeneration = 0;
    std::uint64_t appliedContentRevision = 0;
    std::uint64_t appliedViewRevision = 0;
    std::uint64_t presentedContentRevision = 0;
    std::uint64_t presentedViewRevision = 0;
    std::uint64_t presentedInteractionToken = 0;
    std::uint64_t cellCountContentRevision = 0;
    std::uint64_t viewportRevision = 0;
    std::uint64_t selectionToken = 0;
    std::int32_t previewCellCount = 0;
    std::int32_t selectedGeneIndex = -1;
    std::array<float, 3> viewport{0.0F, 0.0F, 0.0F};
};

class GenomePreviewEndpoint final {
public:
    GenomePreviewEndpoint(std::uint64_t generation, int maximumCellCount);
    ~GenomePreviewEndpoint();

    GenomePreviewEndpoint(const GenomePreviewEndpoint&) = delete;
    GenomePreviewEndpoint& operator=(const GenomePreviewEndpoint&) = delete;

    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uint64_t surfaceCreated() noexcept;
    void surfaceDestroyed(std::uint64_t attachmentGeneration) noexcept;
    [[nodiscard]] std::uint64_t activeAttachmentGeneration() const noexcept;

    [[nodiscard]] GenomePreviewStatePublishResult publishContent(const std::uint8_t* genomeDraftBytes, std::size_t genomeDraftByteCount, double simulationTimeHours, std::uint64_t producerSequence, std::uint64_t expectedGeneration);
    [[nodiscard]] GenomePreviewStatePublishResult publishViewState(const GenomePreviewViewUpdate& update, std::uint64_t producerSequence, std::uint64_t expectedGeneration);
    [[nodiscard]] GenomePreviewInteractionEnqueueResult enqueueInteraction(const GenomePreviewInteractionInput& input, std::uint64_t expectedGeneration);
    [[nodiscard]] GenomePreviewCaptureAdmission enqueueFileCapture(std::uint64_t requestId, const std::uint8_t* genomeDraftBytes, std::size_t genomeDraftByteCount, double maximumSimulationTime, std::uint64_t controllerGeneration, std::uint64_t attachmentGeneration);

    void applyPendingOnRenderOwner(NativeCellularityController& controller);
    [[nodiscard]] cellularity::render::RenderFrameResult draw(NativeCellularityController& controller);
    [[nodiscard]] GenomePreviewAcknowledgment readAcknowledgment() const noexcept;

    void close() noexcept;

private:
    friend class ::NativeCellularityController;

    void applyFenceOnRenderOwner(NativeCellularityController& controller, const GenomePreviewStateFence& fence);
    void applyContentOnRenderOwner(NativeCellularityController& controller, const std::shared_ptr<const GenomePreviewContentSnapshot>& content);
    void applyViewOnRenderOwner(NativeCellularityController& controller, const std::shared_ptr<const GenomePreviewViewSnapshot>& view);
    void applyInteractionOnRenderOwner(NativeCellularityController& controller, const GenomePreviewInteractionCommand& command);
    void executeCapturesOnRenderOwner(NativeCellularityController& controller);
    void publishPresentedAcknowledgment(std::uint64_t attachmentGeneration, cellularity::render::RenderFrameResult result) noexcept;
    void publishViewportAcknowledgment(NativeCellularityController& controller);

    const std::uint64_t mGeneration;
    std::unique_ptr<NativeGenomePreviewFacade> mFacade;
    GenomePreviewRetainedState mRetainedState;
    GenomePreviewInteractionQueue mInteractionQueue;
    GenomePreviewCaptureRequestStore mCaptureStore;
    GenomePreviewFrameRequestFlag mFrameRequest;

    mutable std::mutex mLifecycleMutex;
    std::uint64_t mNextAttachmentGeneration = 1;
    std::uint64_t mActiveAttachmentGeneration = 0;
    bool mClosing = false;

    mutable std::mutex mAcknowledgmentMutex;
    GenomePreviewAcknowledgment mAcknowledgment{};
    std::uint64_t mAppliedInteractionToken = 0;
};

[[nodiscard]] std::shared_ptr<GenomePreviewEndpoint> createGenomePreviewEndpoint(int maximumCellCount);

} // namespace cellularity::app::genome_preview
