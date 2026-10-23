#include "app/EnvironmentFieldPreviewEndpoint.h"

#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cellularity::app {
namespace {

std::atomic<std::uint64_t> gNextEndpointGeneration{1};

std::uint64_t acquireEndpointGeneration() noexcept {
    std::uint64_t candidate =
            gNextEndpointGeneration.load(std::memory_order_relaxed);
    while (candidate != 0) {
        const std::uint64_t next =
                candidate == std::numeric_limits<std::uint64_t>::max()
                ? 0
                : candidate + 1;
        if (gNextEndpointGeneration.compare_exchange_weak(candidate, next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
            return candidate;
        }
    }
    return 0;
}

} // namespace

EnvironmentFieldPreviewEndpoint::EnvironmentFieldPreviewEndpoint(
        std::uint64_t generation, std::uint64_t nextIngressRevision)
        : mGeneration(generation),
          mNextIngressRevision(nextIngressRevision) {
    if (mGeneration == 0) {
        throw std::invalid_argument(
                "Environment Field Preview endpoint generation is zero");
    }
}

EnvironmentFieldPreviewEndpoint::~EnvironmentFieldPreviewEndpoint() {
    close();
}

std::uint64_t EnvironmentFieldPreviewEndpoint::generation() const noexcept {
    return mGeneration;
}

EnvironmentFieldPreviewUpdateResult EnvironmentFieldPreviewEndpoint::publish(
        std::uint64_t expectedGeneration,
        const EnvironmentFieldPreviewUpdate& update) {
    if (validateEnvironmentFieldPreviewUpdate(update)
            != EnvironmentFieldPreviewValidationError::None) {
        return {EnvironmentFieldPreviewUpdateStatus::Malformed, 0};
    }

    std::lock_guard<std::mutex> lock(mIngressMutex);
    if (expectedGeneration == 0 || expectedGeneration != mGeneration) {
        return {EnvironmentFieldPreviewUpdateStatus::StaleEndpoint, 0};
    }
    if (mClosing) {
        return {EnvironmentFieldPreviewUpdateStatus::Closing, 0};
    }

    const std::shared_ptr<const EnvironmentFieldPreviewState> retained =
            std::atomic_load_explicit(
                    &mRetainedState, std::memory_order_acquire);
    if (retained != nullptr
            && environmentFieldPreviewUpdatesEqual(retained->value, update)) {
        return {EnvironmentFieldPreviewUpdateStatus::Duplicate,
                retained->ingressRevision};
    }
    if (mNextIngressRevision == 0) {
        return {EnvironmentFieldPreviewUpdateStatus::RevisionExhausted, 0};
    }

    const std::uint64_t revision = mNextIngressRevision;
    auto next = std::make_shared<const EnvironmentFieldPreviewState>(
            EnvironmentFieldPreviewState{revision, update});
    mNextIngressRevision =
            revision == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : revision + 1;
    std::atomic_store_explicit(
            &mRetainedState, std::move(next), std::memory_order_release);
    return {EnvironmentFieldPreviewUpdateStatus::Applied, revision};
}

std::shared_ptr<const EnvironmentFieldPreviewState>
EnvironmentFieldPreviewEndpoint::retainedState() const noexcept {
    return std::atomic_load_explicit(
            &mRetainedState, std::memory_order_acquire);
}

void EnvironmentFieldPreviewEndpoint::close() noexcept {
    std::lock_guard<std::mutex> lock(mIngressMutex);
    if (mClosing) {
        return;
    }
    mClosing = true;
    std::atomic_store_explicit(
            &mRetainedState,
            std::shared_ptr<const EnvironmentFieldPreviewState>{},
            std::memory_order_release);
}

std::shared_ptr<EnvironmentFieldPreviewEndpoint>
createEnvironmentFieldPreviewEndpoint() {
    const std::uint64_t generation = acquireEndpointGeneration();
    if (generation == 0) {
        throw std::overflow_error(
                "Environment Field Preview endpoint generation exhausted");
    }
    return std::make_shared<EnvironmentFieldPreviewEndpoint>(generation);
}

} // namespace cellularity::app
