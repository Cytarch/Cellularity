#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "app/genome_preview/RetainedPayloadBudget.h"
#include "app/render/preview/NativePreviewCaptureLayouts.h"

namespace cellularity::app::preview {

enum class PreviewCaptureRequestKind : std::uint8_t {
    Genome = 0,
    Substrate = 1,
};

enum class PreviewCaptureRequestPhase : std::uint8_t {
    Pending = 0,
    Running = 1,
    Completed = 2,
    Failed = 3,
    Cancelled = 4,
};

[[nodiscard]] constexpr bool isTerminal(PreviewCaptureRequestPhase phase) noexcept {
    return phase == PreviewCaptureRequestPhase::Completed || phase == PreviewCaptureRequestPhase::Failed || phase == PreviewCaptureRequestPhase::Cancelled;
}

struct PreviewCapturePayload final {
    PreviewCapturePayload(PreviewCaptureResultHeader headerValue, std::vector<std::uint8_t> bytesValue, genome_preview::RetainedPayloadBudget::Reservation reservationValue);

    const PreviewCaptureResultHeader header;
    const std::vector<std::uint8_t> bytes;

private:
    genome_preview::RetainedPayloadBudget::Reservation mReservation;
};

struct PreviewCaptureAwaitTicket final {
    PreviewCaptureResultHeader header{};
    std::shared_ptr<const PreviewCapturePayload> payload;
    std::uint64_t deliveryToken = 0;
};

class PreviewCaptureRequestState {
public:
    PreviewCaptureRequestState(std::uint64_t requestIdValue, PreviewCaptureRequestKind kindValue, std::uint64_t controllerGenerationValue, std::uint64_t attachmentGenerationValue) noexcept;
    virtual ~PreviewCaptureRequestState() = default;

    PreviewCaptureRequestState(const PreviewCaptureRequestState&) = delete;
    PreviewCaptureRequestState& operator=(const PreviewCaptureRequestState&) = delete;

    [[nodiscard]] bool tryMarkRunning() noexcept;
    [[nodiscard]] bool complete(PreviewCaptureResultHeader header, std::vector<std::uint8_t> bytes, genome_preview::RetainedPayloadBudget::Reservation reservation) noexcept;
    [[nodiscard]] bool completeForTest() noexcept;
    [[nodiscard]] bool fail() noexcept;
    [[nodiscard]] PreviewCaptureCancelResult cancel() noexcept;
    [[nodiscard]] PreviewCaptureRequestPhase phase() const noexcept;
    [[nodiscard]] PreviewCaptureRequestPhase waitForTerminal(std::chrono::milliseconds timeout) const noexcept;
    [[nodiscard]] PreviewCaptureAwaitTicket await(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] bool consumeDelivery(std::uint64_t deliveryToken) noexcept;
    void abandonDelivery(std::uint64_t deliveryToken) noexcept;

    const std::uint64_t requestId;
    const PreviewCaptureRequestKind kind;
    const std::uint64_t controllerGeneration;
    const std::uint64_t attachmentGeneration;

private:
    [[nodiscard]] bool transitionToTerminal(PreviewCaptureRequestPhase terminal) noexcept;
    [[nodiscard]] PreviewCaptureResultHeader terminalHeaderLocked() const noexcept;

    mutable std::mutex mMutex;
    mutable std::condition_variable mTerminalCondition;
    PreviewCaptureRequestPhase mPhase = PreviewCaptureRequestPhase::Pending;
    std::shared_ptr<const PreviewCapturePayload> mPayload;
    bool mConsumed = false;
    bool mAwaitActive = false;
    std::uint64_t mNextDeliveryToken = 1;
    std::uint64_t mActiveDeliveryToken = 0;
};

} // namespace cellularity::app::preview
