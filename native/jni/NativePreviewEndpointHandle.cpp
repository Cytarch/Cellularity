#include "NativePreviewEndpointHandle.h"

#include <stdexcept>
#include <utility>

#include "app/EnvironmentFieldPreviewEndpoint.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"

namespace {

NativePreviewEndpointHandle* handleFromValue(std::uint64_t value) noexcept {
    return reinterpret_cast<NativePreviewEndpointHandle*>(
            static_cast<std::uintptr_t>(value));
}

std::uint64_t handleToValue(NativePreviewEndpointHandle* value) noexcept {
    return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(value));
}

} // namespace

NativePreviewEndpointHandle::NativePreviewEndpointHandle(
        std::shared_ptr<
                cellularity::app::genome_preview::GenomePreviewEndpoint>
                endpoint,
        std::shared_ptr<NativeCellularityController> controller)
        : mKind(PreviewEndpointKind::GenomePreview),
          mGeneration(endpoint == nullptr ? 0 : endpoint->generation()),
          mPayload(std::move(endpoint)),
          mController(std::move(controller)) {
    if (mGeneration == 0
            || std::get<0>(mPayload) == nullptr) {
        throw std::invalid_argument(
                "Genome Preview endpoint handle requires a live endpoint");
    }
}

NativePreviewEndpointHandle::NativePreviewEndpointHandle(
        std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
                endpoint,
        std::shared_ptr<NativeCellularityController> controller)
        : mKind(PreviewEndpointKind::EnvironmentFieldPreview),
          mGeneration(endpoint == nullptr ? 0 : endpoint->generation()),
          mPayload(std::move(endpoint)),
          mController(std::move(controller)) {
    if (mGeneration == 0
            || std::get<1>(mPayload) == nullptr) {
        throw std::invalid_argument(
                "Environment Field Preview endpoint handle requires a live endpoint");
    }
}

PreviewEndpointKind NativePreviewEndpointHandle::kind() const noexcept {
    return mKind;
}

std::uint64_t NativePreviewEndpointHandle::generation() const noexcept {
    return mGeneration;
}

PreviewEndpointAcquireResult<
        cellularity::app::genome_preview::GenomePreviewEndpoint>
NativePreviewEndpointHandle::acquireGenome(
        std::uint64_t expectedGeneration) const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        return {PreviewEndpointAcquireStatus::Closed, nullptr};
    }
    if (mKind != PreviewEndpointKind::GenomePreview) {
        return {PreviewEndpointAcquireStatus::WrongKind, nullptr};
    }
    if (expectedGeneration == 0 || expectedGeneration != mGeneration) {
        return {PreviewEndpointAcquireStatus::StaleGeneration, nullptr};
    }
    return {PreviewEndpointAcquireStatus::Ready, std::get<0>(mPayload)};
}

PreviewEndpointAcquireResult<
        cellularity::app::EnvironmentFieldPreviewEndpoint>
NativePreviewEndpointHandle::acquireEnvironmentField(
        std::uint64_t expectedGeneration) const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        return {PreviewEndpointAcquireStatus::Closed, nullptr};
    }
    if (mKind != PreviewEndpointKind::EnvironmentFieldPreview) {
        return {PreviewEndpointAcquireStatus::WrongKind, nullptr};
    }
    if (expectedGeneration == 0 || expectedGeneration != mGeneration) {
        return {PreviewEndpointAcquireStatus::StaleGeneration, nullptr};
    }
    return {PreviewEndpointAcquireStatus::Ready, std::get<1>(mPayload)};
}

std::shared_ptr<NativeCellularityController>
NativePreviewEndpointHandle::acquireController(
        std::uint64_t expectedGeneration) const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed || expectedGeneration == 0
            || expectedGeneration != mGeneration) {
        return nullptr;
    }
    return mController;
}

NativePreviewEndpointPayload NativePreviewEndpointHandle::closeAndTake() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        return mKind == PreviewEndpointKind::GenomePreview
                ? NativePreviewEndpointPayload{
                          std::shared_ptr<cellularity::app::genome_preview::
                                          GenomePreviewEndpoint>{}
                  }
                : NativePreviewEndpointPayload{std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>{}};
    }
    mClosed = true;
    mController.reset();
    NativePreviewEndpointPayload result = std::move(mPayload);
    mPayload = mKind == PreviewEndpointKind::GenomePreview
            ? NativePreviewEndpointPayload{
                      std::shared_ptr<cellularity::app::genome_preview::
                                      GenomePreviewEndpoint>{}
              }
            : NativePreviewEndpointPayload{std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>{}};
    return result;
}

std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<
                cellularity::app::genome_preview::GenomePreviewEndpoint>
                endpoint) {
    return handleToValue(
            new NativePreviewEndpointHandle(std::move(endpoint)));
}

std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<
                cellularity::app::genome_preview::GenomePreviewEndpoint>
                endpoint,
        std::shared_ptr<NativeCellularityController> controller) {
    return handleToValue(new NativePreviewEndpointHandle(
            std::move(endpoint), std::move(controller)));
}

std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
                endpoint,
        std::shared_ptr<NativeCellularityController> controller) {
    return handleToValue(new NativePreviewEndpointHandle(
            std::move(endpoint), std::move(controller)));
}

std::uint64_t createNativePreviewEndpointHandle(
        std::shared_ptr<cellularity::app::EnvironmentFieldPreviewEndpoint>
                endpoint) {
    return handleToValue(
            new NativePreviewEndpointHandle(std::move(endpoint)));
}

PreviewEndpointAcquireResult<
        cellularity::app::genome_preview::GenomePreviewEndpoint>
acquireGenomePreviewEndpoint(
        std::uint64_t value,
        std::uint64_t expectedGeneration) {
    NativePreviewEndpointHandle* handle = handleFromValue(value);
    return handle == nullptr
            ? PreviewEndpointAcquireResult<cellularity::app::genome_preview::
                              GenomePreviewEndpoint>{
                      PreviewEndpointAcquireStatus::InvalidHandle, nullptr
              }
            : handle->acquireGenome(expectedGeneration);
}

PreviewEndpointAcquireResult<
        cellularity::app::EnvironmentFieldPreviewEndpoint>
acquireEnvironmentFieldPreviewEndpoint(
        std::uint64_t value,
        std::uint64_t expectedGeneration) {
    NativePreviewEndpointHandle* handle = handleFromValue(value);
    return handle == nullptr
            ? PreviewEndpointAcquireResult<
                      cellularity::app::EnvironmentFieldPreviewEndpoint>{
                      PreviewEndpointAcquireStatus::InvalidHandle, nullptr
              }
            : handle->acquireEnvironmentField(expectedGeneration);
}

std::shared_ptr<NativeCellularityController> acquirePreviewController(
        std::uint64_t value,
        std::uint64_t expectedGeneration) {
    NativePreviewEndpointHandle* handle = handleFromValue(value);
    return handle == nullptr
            ? nullptr
            : handle->acquireController(expectedGeneration);
}

PreviewEndpointKind nativePreviewEndpointKind(std::uint64_t value) {
    NativePreviewEndpointHandle* handle = handleFromValue(value);
    if (handle == nullptr) {
        throw std::invalid_argument("Native preview endpoint handle is null");
    }
    return handle->kind();
}

std::uint64_t nativePreviewEndpointGeneration(std::uint64_t value) {
    NativePreviewEndpointHandle* handle = handleFromValue(value);
    return handle == nullptr ? 0 : handle->generation();
}

void closeAndDestroyNativePreviewEndpointHandle(
        std::uint64_t value) noexcept {
    NativePreviewEndpointHandle* handle = handleFromValue(value);
    if (handle == nullptr) {
        return;
    }
    auto payload = handle->closeAndTake();
    delete handle;
    std::visit([](auto& endpoint) {
        endpoint.reset();
    },
            payload);
}
