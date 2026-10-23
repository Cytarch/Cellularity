#include "app/render/preview/NativePreviewCaptureRequestState.h"

#include <limits>
#include <utility>

namespace cellularity::app::preview {

PreviewCapturePayload::PreviewCapturePayload(PreviewCaptureResultHeader headerValue, std::vector<std::uint8_t> bytesValue, genome_preview::RetainedPayloadBudget::Reservation reservationValue)
        : header(headerValue), bytes(std::move(bytesValue)), mReservation(std::move(reservationValue)) {
}

PreviewCaptureRequestState::PreviewCaptureRequestState(std::uint64_t requestIdValue, PreviewCaptureRequestKind kindValue, std::uint64_t controllerGenerationValue, std::uint64_t attachmentGenerationValue) noexcept
        : requestId(requestIdValue), kind(kindValue), controllerGeneration(controllerGenerationValue), attachmentGeneration(attachmentGenerationValue) {
}

bool PreviewCaptureRequestState::tryMarkRunning() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mPhase != PreviewCaptureRequestPhase::Pending) {
        return false;
    }
    mPhase = PreviewCaptureRequestPhase::Running;
    return true;
}

bool PreviewCaptureRequestState::complete(PreviewCaptureResultHeader header, std::vector<std::uint8_t> bytes, genome_preview::RetainedPayloadBudget::Reservation reservation) noexcept {
    if (header.requestId != requestId || !validateCompletedPreviewCaptureResult(header, bytes.size()) || reservation.bytes() != bytes.size()) {
        return fail();
    }
    std::shared_ptr<const PreviewCapturePayload> payload;
    try {
        payload = std::make_shared<const PreviewCapturePayload>(header, std::move(bytes), std::move(reservation));
    } catch (...) {
        return fail();
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (isTerminal(mPhase)) {
        return false;
    }
    mPayload = std::move(payload);
    mPhase = PreviewCaptureRequestPhase::Completed;
    mTerminalCondition.notify_all();
    return true;
}

bool PreviewCaptureRequestState::completeForTest() noexcept {
    return transitionToTerminal(PreviewCaptureRequestPhase::Completed);
}

bool PreviewCaptureRequestState::fail() noexcept {
    return transitionToTerminal(PreviewCaptureRequestPhase::Failed);
}

PreviewCaptureCancelResult PreviewCaptureRequestState::cancel() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mPhase == PreviewCaptureRequestPhase::Cancelled) {
        return PreviewCaptureCancelResult::AlreadyCancelled;
    }
    if (isTerminal(mPhase)) {
        return PreviewCaptureCancelResult::AlreadyTerminal;
    }
    mPhase = PreviewCaptureRequestPhase::Cancelled;
    mTerminalCondition.notify_all();
    return PreviewCaptureCancelResult::CancellationRequested;
}

PreviewCaptureRequestPhase PreviewCaptureRequestState::phase() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mPhase;
}

PreviewCaptureRequestPhase PreviewCaptureRequestState::waitForTerminal(std::chrono::milliseconds timeout) const noexcept {
    std::unique_lock<std::mutex> lock(mMutex);
    (void)mTerminalCondition.wait_for(lock, timeout, [this] {
        return isTerminal(mPhase);
    });
    return mPhase;
}

PreviewCaptureAwaitTicket PreviewCaptureRequestState::await(std::chrono::milliseconds timeout) noexcept {
    PreviewCaptureAwaitTicket ticket;
    ticket.header.requestId = requestId;
    if (timeout.count() <= 0) {
        ticket.header.status = PreviewCaptureStatus::InternalFailure;
        return ticket;
    }

    std::unique_lock<std::mutex> lock(mMutex);
    if (mConsumed) {
        ticket.header.status = PreviewCaptureStatus::AlreadyConsumed;
        return ticket;
    }
    if (mAwaitActive) {
        ticket.header.status = PreviewCaptureStatus::InternalFailure;
        return ticket;
    }
    mAwaitActive = true;
    const bool terminal = mTerminalCondition.wait_for(lock, timeout, [this] {
        return isTerminal(mPhase);
    });
    if (!terminal) {
        mAwaitActive = false;
        ticket.header.status = PreviewCaptureStatus::TimedOut;
        return ticket;
    }

    ticket.header = terminalHeaderLocked();
    ticket.payload = mPayload;
    if (mNextDeliveryToken == 0) {
        mAwaitActive = false;
        ticket.header = {requestId, PreviewCaptureStatus::InternalFailure};
        ticket.payload.reset();
        return ticket;
    }
    ticket.deliveryToken = mNextDeliveryToken;
    mActiveDeliveryToken = ticket.deliveryToken;
    mNextDeliveryToken = ticket.deliveryToken == std::numeric_limits<std::uint64_t>::max() ? 0 : ticket.deliveryToken + 1U;
    return ticket;
}

bool PreviewCaptureRequestState::consumeDelivery(std::uint64_t deliveryToken) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mAwaitActive || deliveryToken == 0 || deliveryToken != mActiveDeliveryToken || mConsumed) {
        return false;
    }
    mConsumed = true;
    mAwaitActive = false;
    mActiveDeliveryToken = 0;
    mPayload.reset();
    mTerminalCondition.notify_all();
    return true;
}

void PreviewCaptureRequestState::abandonDelivery(std::uint64_t deliveryToken) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mAwaitActive && deliveryToken != 0 && deliveryToken == mActiveDeliveryToken && !mConsumed) {
        mAwaitActive = false;
        mActiveDeliveryToken = 0;
        mTerminalCondition.notify_all();
    }
}

bool PreviewCaptureRequestState::transitionToTerminal(PreviewCaptureRequestPhase terminal) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!isTerminal(terminal) || isTerminal(mPhase)) {
        return false;
    }
    mPhase = terminal;
    mTerminalCondition.notify_all();
    return true;
}

PreviewCaptureResultHeader PreviewCaptureRequestState::terminalHeaderLocked() const noexcept {
    if (mPhase == PreviewCaptureRequestPhase::Completed && mPayload != nullptr) {
        return mPayload->header;
    }
    PreviewCaptureResultHeader header;
    header.requestId = requestId;
    switch (mPhase) {
        case PreviewCaptureRequestPhase::Completed:
            header.status = PreviewCaptureStatus::Completed;
            break;
        case PreviewCaptureRequestPhase::Failed:
            header.status = PreviewCaptureStatus::Failed;
            break;
        case PreviewCaptureRequestPhase::Cancelled:
            header.status = PreviewCaptureStatus::Cancelled;
            break;
        case PreviewCaptureRequestPhase::Pending:
        case PreviewCaptureRequestPhase::Running:
            header.status = PreviewCaptureStatus::InternalFailure;
            break;
    }
    return header;
}

} // namespace cellularity::app::preview
