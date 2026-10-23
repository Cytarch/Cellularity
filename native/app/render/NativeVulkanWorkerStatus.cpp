#include "app/render/NativeVulkanWorkerStatus.h"

#include <cstring>

namespace cellularity::app::render_worker {
namespace {

template <typename T>
void writeLittleEndian(std::uint8_t* destination,
        std::size_t offset, T value) noexcept {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        destination[offset + index] = static_cast<std::uint8_t>(
                static_cast<std::uint64_t>(value) >> (index * 8U));
    }
}

} // namespace

void encodeNativeVulkanWorkerStatus(
        const NativeVulkanWorkerStatus& status,
        std::uint8_t* destination) noexcept {
    std::memset(destination, 0, kNativeVulkanWorkerStatusSize);
    writeLittleEndian(destination, 0, kNativeVulkanWorkerStatusMagic);
    writeLittleEndian(destination, 4, kNativeVulkanWorkerStatusVersion);
    writeLittleEndian(destination, 6,
            static_cast<std::uint16_t>(kNativeVulkanWorkerStatusSize));
    writeLittleEndian(destination, 8, status.revision);
    writeLittleEndian(destination, 16,
            static_cast<std::uint32_t>(status.phase));
    writeLittleEndian(destination, 20, status.lastFrameOutcome);
    writeLittleEndian(destination, 24, status.flags);
    writeLittleEndian(destination, 28,
            static_cast<std::uint32_t>(status.failure));
    writeLittleEndian(destination, 32, status.presentedFrames);
    writeLittleEndian(destination, 40, status.swapchainRecreations);
    writeLittleEndian(destination, 48, status.failedFrameAttempts);
    writeLittleEndian(destination, 56, status.desiredSurfaceGeneration);
    writeLittleEndian(destination, 64, status.appliedSurfaceGeneration);
    writeLittleEndian(destination, 72, status.diagnosticsRevision);
    writeLittleEndian(destination, 80, status.desiredWidth);
    writeLittleEndian(destination, 84, status.desiredHeight);
}

} // namespace cellularity::app::render_worker
