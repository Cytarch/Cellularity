#include "app/genome_preview/RetainedPayloadBudget.h"

#include <mutex>
#include <stdexcept>
#include <utility>

namespace cellularity::app::genome_preview {

struct RetainedPayloadBudget::Ledger final {
    explicit Ledger(std::size_t capacity)
            : capacityBytes(capacity) {
    }

    const std::size_t capacityBytes;
    mutable std::mutex mutex;
    std::size_t retainedBytes = 0;
    std::size_t peakRetainedBytes = 0;
    std::size_t activeReservations = 0;
};

RetainedPayloadBudget::Reservation::Reservation(
        std::shared_ptr<Ledger> ledger, std::size_t bytes) noexcept
        : mLedger(std::move(ledger)), mBytes(bytes) {
}

RetainedPayloadBudget::Reservation::~Reservation() {
    reset();
}

RetainedPayloadBudget::Reservation::Reservation(
        Reservation&& other) noexcept
        : mLedger(std::move(other.mLedger)), mBytes(other.mBytes) {
    other.mBytes = 0;
}

RetainedPayloadBudget::Reservation&
RetainedPayloadBudget::Reservation::operator=(Reservation&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    mLedger = std::move(other.mLedger);
    mBytes = other.mBytes;
    other.mBytes = 0;
    return *this;
}

std::size_t RetainedPayloadBudget::Reservation::bytes() const noexcept {
    return mBytes;
}

RetainedPayloadBudget::Reservation::operator bool() const noexcept {
    return mLedger != nullptr;
}

void RetainedPayloadBudget::Reservation::reset() noexcept {
    if (mLedger == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mLedger->mutex);
        if (mBytes <= mLedger->retainedBytes) {
            mLedger->retainedBytes -= mBytes;
        } else {
            mLedger->retainedBytes = 0;
        }
        if (mLedger->activeReservations > 0) {
            --mLedger->activeReservations;
        }
    }
    mLedger.reset();
    mBytes = 0;
}

RetainedPayloadBudget::RetainedPayloadBudget(std::size_t capacityBytes)
        : mLedger(std::make_shared<Ledger>(capacityBytes)) {
    if (capacityBytes == 0) {
        throw std::invalid_argument(
                "Retained payload budget capacity must be positive");
    }
}

std::optional<RetainedPayloadBudget::Reservation>
RetainedPayloadBudget::tryReserve(std::size_t bytes) noexcept {
    if (bytes == 0) {
        return std::nullopt;
    }
    {
        std::lock_guard<std::mutex> lock(mLedger->mutex);
        if (bytes > mLedger->capacityBytes - mLedger->retainedBytes) {
            return std::nullopt;
        }
        mLedger->retainedBytes += bytes;
        if (mLedger->retainedBytes > mLedger->peakRetainedBytes) {
            mLedger->peakRetainedBytes = mLedger->retainedBytes;
        }
        ++mLedger->activeReservations;
    }
    Reservation reservation(mLedger, bytes);
    return std::optional<Reservation>(std::move(reservation));
}

RetainedPayloadBudgetSnapshot
RetainedPayloadBudget::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mLedger->mutex);
    return {
            mLedger->capacityBytes,
            mLedger->retainedBytes,
            mLedger->peakRetainedBytes,
            mLedger->activeReservations,
    };
}

} // namespace cellularity::app::genome_preview
