#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace cellularity::app::genome_preview {

[[nodiscard]] constexpr bool isPowerOfTwo(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

/**
 * Selects the smallest power of two covering twice an observed high-water
 * mark. Zero means that the inputs overflow, violate the power-of-two policy,
 * or exceed the explicit hard cap; the function never clamps.
 */
[[nodiscard]] constexpr std::size_t boundedCapacityFromObservedHighWater(
        std::size_t observedMaximumHighWater,
        std::size_t minimumCapacity,
        std::size_t hardCap) noexcept {
    if (!isPowerOfTwo(minimumCapacity)
            || !isPowerOfTwo(hardCap)
            || minimumCapacity > hardCap
            || observedMaximumHighWater
                    > std::numeric_limits<std::size_t>::max() / 2U) {
        return 0;
    }
    const std::size_t doubled = observedMaximumHighWater * 2U;
    const std::size_t required =
            doubled > minimumCapacity ? doubled : minimumCapacity;
    std::size_t capacity = minimumCapacity;
    while (capacity < required) {
        if (capacity > hardCap / 2U) {
            return 0;
        }
        capacity *= 2U;
    }
    return capacity <= hardCap ? capacity : 0;
}

[[nodiscard]] constexpr std::size_t checkedSizeProductOrZero(
        std::size_t left, std::size_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    if (left > std::numeric_limits<std::size_t>::max() / right) {
        return 0;
    }
    return left * right;
}

struct RetainedPayloadBudgetSnapshot final {
    std::size_t capacityBytes = 0;
    std::size_t retainedBytes = 0;
    std::size_t peakRetainedBytes = 0;
    std::size_t activeReservations = 0;
};

/**
 * A local accounting ledger for immutable retained payloads. One move-only
 * reservation lives inside one immutable allocation; copying shared_ptr
 * fences therefore never charges the same bytes twice.
 */
class RetainedPayloadBudget final {
private:
    struct Ledger;

public:
    class Reservation final {
    public:
        Reservation() noexcept = default;
        ~Reservation();

        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;

        Reservation(Reservation&& other) noexcept;
        Reservation& operator=(Reservation&& other) noexcept;

        [[nodiscard]] std::size_t bytes() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        void reset() noexcept;

    private:
        friend class RetainedPayloadBudget;

        Reservation(std::shared_ptr<Ledger> ledger,
                std::size_t bytes) noexcept;

        std::shared_ptr<Ledger> mLedger;
        std::size_t mBytes = 0;
    };

    explicit RetainedPayloadBudget(std::size_t capacityBytes);
    ~RetainedPayloadBudget() = default;

    RetainedPayloadBudget(const RetainedPayloadBudget&) = delete;
    RetainedPayloadBudget& operator=(const RetainedPayloadBudget&) = delete;
    RetainedPayloadBudget(RetainedPayloadBudget&&) = delete;
    RetainedPayloadBudget& operator=(RetainedPayloadBudget&&) = delete;

    [[nodiscard]] std::optional<Reservation>
    tryReserve(std::size_t bytes) noexcept;
    [[nodiscard]] RetainedPayloadBudgetSnapshot snapshot() const noexcept;

private:
    std::shared_ptr<Ledger> mLedger;
};

} // namespace cellularity::app::genome_preview
