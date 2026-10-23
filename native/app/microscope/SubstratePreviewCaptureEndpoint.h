#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "app/microscope/SubstratePreviewCaptureRequestStore.h"

class NativeCellularityController;

namespace cellularity::app::microscope {

class SubstratePreviewCaptureEndpoint final {
public:
    explicit SubstratePreviewCaptureEndpoint(std::uint64_t generation);
    ~SubstratePreviewCaptureEndpoint();

    SubstratePreviewCaptureEndpoint(const SubstratePreviewCaptureEndpoint&) = delete;
    SubstratePreviewCaptureEndpoint& operator=(const SubstratePreviewCaptureEndpoint&) = delete;

    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uint64_t surfaceCreated() noexcept;
    void surfaceDestroyed(std::uint64_t attachmentGeneration) noexcept;
    [[nodiscard]] std::uint64_t activeAttachmentGeneration() const noexcept;

    [[nodiscard]] SubstratePreviewCaptureAdmission enqueue(std::uint64_t requestId, std::uint64_t substratePreviewToken, std::uint64_t attachmentGeneration);
    void applyPendingOnRenderOwner(NativeCellularityController& controller);
    void close() noexcept;

private:
    const std::uint64_t mGeneration;
    SubstratePreviewCaptureRequestStore mStore;
    mutable std::mutex mLifecycleMutex;
    std::uint64_t mNextAttachmentGeneration = 1;
    std::uint64_t mActiveAttachmentGeneration = 0;
    bool mClosing = false;
};

[[nodiscard]] std::shared_ptr<SubstratePreviewCaptureEndpoint> createSubstratePreviewCaptureEndpoint();

} // namespace cellularity::app::microscope
