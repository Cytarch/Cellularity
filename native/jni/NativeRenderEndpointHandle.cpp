#include "NativeRenderEndpointHandle.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "app/render/NativeRenderAttachmentLease.h"
#include "platform/android/AndroidRenderAssetsLease.h"

namespace {

NativeRenderEndpointHandle* handleFromValue(std::uint64_t value) noexcept {
    return reinterpret_cast<NativeRenderEndpointHandle*>(
            static_cast<std::uintptr_t>(value));
}

std::uint64_t handleToValue(NativeRenderEndpointHandle* value) noexcept {
    return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(value));
}

NativeRenderEndpointHandle* requireHandle(std::uint64_t value) {
    NativeRenderEndpointHandle* handle = handleFromValue(value);
    if (handle == nullptr) {
        throw std::invalid_argument(
                "Native render endpoint handle is null");
    }
    return handle;
}

} // namespace

NativeRenderEndpointHandle::NativeRenderEndpointHandle(
        std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
                attachmentLease,
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease)
        : mAttachmentLease(std::move(attachmentLease)),
          mAssetsLease(std::move(assetsLease)) {
    if (mAttachmentLease == nullptr || mAssetsLease == nullptr) {
        throw std::invalid_argument(
                "Native render endpoint requires attachment and assets leases");
    }
}

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
NativeRenderEndpointHandle::acquireAttachmentLease() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed ? nullptr : mAttachmentLease;
}

std::shared_ptr<AndroidRenderAssetsLease>
NativeRenderEndpointHandle::acquireAssetsLease() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed ? nullptr : mAssetsLease;
}

void NativeRenderEndpointHandle::close() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        return;
    }
    mClosed = true;
    mAssetsLease.reset();
    mAttachmentLease.reset();
}

std::uint64_t createNativeRenderEndpointHandle(
        std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
                attachmentLease,
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease) {
    return handleToValue(new NativeRenderEndpointHandle(
            std::move(attachmentLease), std::move(assetsLease)));
}

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
renderEndpointLeaseFromHandle(std::uint64_t endpointHandle) {
    NativeRenderEndpointHandle* handle = requireHandle(endpointHandle);
    auto lease = handle->acquireAttachmentLease();
    if (lease == nullptr) {
        throw std::logic_error("Native render endpoint is closed");
    }
    return lease;
}

std::shared_ptr<AndroidRenderAssetsLease>
renderEndpointAssetsFromHandle(std::uint64_t endpointHandle) {
    NativeRenderEndpointHandle* handle = requireHandle(endpointHandle);
    auto lease = handle->acquireAssetsLease();
    if (lease == nullptr) {
        throw std::logic_error("Native render endpoint assets are closed");
    }
    return lease;
}

void closeAndDestroyNativeRenderEndpointHandle(
        std::uint64_t endpointHandle) noexcept {
    NativeRenderEndpointHandle* handle = handleFromValue(endpointHandle);
    if (handle == nullptr) {
        return;
    }
    handle->close();
    delete handle;
}
