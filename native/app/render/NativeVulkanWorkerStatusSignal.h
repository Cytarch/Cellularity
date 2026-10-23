#pragma once

#include <cstdint>

namespace cellularity::app::render_worker {

enum NativeVulkanWorkerAction : std::uint32_t {
    kWorkerActionSurfaceReattachRequired = 1U << 0,
    kWorkerActionTerminalFailure = 1U << 1,
    kWorkerActionRecoverableFailure = 1U << 2,
};

enum class StatusSignalDelivery : std::uint8_t {
    Accepted = 0,
    ListenerClosed = 1,
    RetryableFailure = 2,
    InvalidSignal = 3,
};

class NativeVulkanWorkerStatusSignal {
public:
    virtual ~NativeVulkanWorkerStatusSignal() = default;
    [[nodiscard]] virtual StatusSignalDelivery deliver(
            std::uint64_t statusRevision,
            std::uint32_t actionBits) noexcept = 0;
};

} // namespace cellularity::app::render_worker
