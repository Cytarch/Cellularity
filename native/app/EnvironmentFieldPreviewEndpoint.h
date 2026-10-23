#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "app/EnvironmentFieldPreviewState.h"

namespace cellularity::app {

class EnvironmentFieldPreviewEndpoint final {
public:
    explicit EnvironmentFieldPreviewEndpoint(std::uint64_t generation,
            std::uint64_t nextIngressRevision = 1);
    ~EnvironmentFieldPreviewEndpoint();

    EnvironmentFieldPreviewEndpoint(const EnvironmentFieldPreviewEndpoint&) =
            delete;
    EnvironmentFieldPreviewEndpoint& operator=(
            const EnvironmentFieldPreviewEndpoint&) = delete;

    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] EnvironmentFieldPreviewUpdateResult publish(
            std::uint64_t expectedGeneration,
            const EnvironmentFieldPreviewUpdate& update);
    [[nodiscard]] std::shared_ptr<const EnvironmentFieldPreviewState>
    retainedState() const noexcept;
    void close() noexcept;

private:
    const std::uint64_t mGeneration;
    mutable std::mutex mIngressMutex;
    std::shared_ptr<const EnvironmentFieldPreviewState> mRetainedState;
    std::uint64_t mNextIngressRevision;
    bool mClosing = false;
};

[[nodiscard]] std::shared_ptr<EnvironmentFieldPreviewEndpoint>
createEnvironmentFieldPreviewEndpoint();

} // namespace cellularity::app
