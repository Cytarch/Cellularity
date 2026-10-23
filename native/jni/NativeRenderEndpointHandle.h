#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

class AndroidRenderAssetsLease;

namespace cellularity::app {
class NativeRenderAttachmentLease;
} // namespace cellularity::app

class NativeRenderEndpointHandle final {
public:
    NativeRenderEndpointHandle(
            std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
                    attachmentLease,
            std::shared_ptr<AndroidRenderAssetsLease> assetsLease);

    NativeRenderEndpointHandle(const NativeRenderEndpointHandle&) = delete;
    NativeRenderEndpointHandle& operator=(
            const NativeRenderEndpointHandle&) = delete;

    [[nodiscard]] std::shared_ptr<
            cellularity::app::NativeRenderAttachmentLease>
    acquireAttachmentLease() const;
    [[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease>
    acquireAssetsLease() const;
    void close() noexcept;

private:
    mutable std::mutex mMutex;
    std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
            mAttachmentLease;
    std::shared_ptr<AndroidRenderAssetsLease> mAssetsLease;
    bool mClosed = false;
};

[[nodiscard]] std::uint64_t createNativeRenderEndpointHandle(
        std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
                attachmentLease,
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease);

[[nodiscard]] std::shared_ptr<
        cellularity::app::NativeRenderAttachmentLease>
renderEndpointLeaseFromHandle(std::uint64_t endpointHandle);

[[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease>
renderEndpointAssetsFromHandle(std::uint64_t endpointHandle);

void closeAndDestroyNativeRenderEndpointHandle(
        std::uint64_t endpointHandle) noexcept;
