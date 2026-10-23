#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <variant>

namespace cellularity::app {
class EnvironmentFieldPreviewEndpoint;

namespace genome_preview {
class GenomePreviewEndpoint;
} // namespace genome_preview
} // namespace cellularity::app

class NativeCellularityController;

enum class PreviewEndpointKind : std::uint8_t {
    GenomePreview = 0,
    EnvironmentFieldPreview = 1,
};

enum class PreviewEndpointAcquireStatus : std::uint8_t {
    Ready = 0,
    WrongKind = 1,
    StaleGeneration = 2,
    Closed = 3,
    InvalidHandle = 4,
};

template <typename T>
struct PreviewEndpointAcquireResult final {
    PreviewEndpointAcquireStatus status =
            PreviewEndpointAcquireStatus::InvalidHandle;
    std::shared_ptr<T> endpoint;
};

using NativePreviewEndpointPayload = std::variant<
        std::shared_ptr<
                cellularity::app::genome_preview::GenomePreviewEndpoint>,
        std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>>;

class NativePreviewEndpointHandle final {
public:
    explicit NativePreviewEndpointHandle(
            std::shared_ptr<
                    cellularity::app::genome_preview::GenomePreviewEndpoint>
                    endpoint,
            std::shared_ptr<NativeCellularityController> controller = nullptr);
    explicit NativePreviewEndpointHandle(
            std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
                    endpoint,
            std::shared_ptr<NativeCellularityController> controller = nullptr);

    [[nodiscard]] PreviewEndpointKind kind() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] PreviewEndpointAcquireResult<
            cellularity::app::genome_preview::GenomePreviewEndpoint>
    acquireGenome(std::uint64_t expectedGeneration) const;
    [[nodiscard]] PreviewEndpointAcquireResult<
            cellularity::app::EnvironmentFieldPreviewEndpoint>
    acquireEnvironmentField(std::uint64_t expectedGeneration) const;
    [[nodiscard]] std::shared_ptr<NativeCellularityController>
    acquireController(std::uint64_t expectedGeneration) const;
    [[nodiscard]] NativePreviewEndpointPayload closeAndTake();

private:
    const PreviewEndpointKind mKind;
    const std::uint64_t mGeneration;
    mutable std::mutex mMutex;
    NativePreviewEndpointPayload mPayload;
    std::shared_ptr<NativeCellularityController> mController;
    bool mClosed = false;
};

[[nodiscard]] std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<
                cellularity::app::genome_preview::GenomePreviewEndpoint>
                endpoint);
[[nodiscard]] std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
                endpoint);
[[nodiscard]] std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<
                cellularity::app::genome_preview::GenomePreviewEndpoint>
                endpoint,
        std::shared_ptr<NativeCellularityController> controller);
[[nodiscard]] std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
                endpoint,
        std::shared_ptr<NativeCellularityController> controller);
[[nodiscard]] PreviewEndpointAcquireResult<
        cellularity::app::genome_preview::GenomePreviewEndpoint>
acquireGenomePreviewEndpoint(
        std::uint64_t handle,
        std::uint64_t expectedGeneration);
[[nodiscard]] PreviewEndpointAcquireResult<
        cellularity::app::EnvironmentFieldPreviewEndpoint>
acquireEnvironmentFieldPreviewEndpoint(
        std::uint64_t handle,
        std::uint64_t expectedGeneration);
[[nodiscard]] std::shared_ptr<NativeCellularityController>
acquirePreviewController(
        std::uint64_t handle,
        std::uint64_t expectedGeneration);
[[nodiscard]] PreviewEndpointKind nativePreviewEndpointKind(
        std::uint64_t handle);
[[nodiscard]] std::uint64_t nativePreviewEndpointGeneration(
        std::uint64_t handle);
void closeAndDestroyNativePreviewEndpointHandle(
        std::uint64_t handle) noexcept;
