#include "NativeControllerHandle.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "NativeJniUtils.h"
#include "app/NativeCellularityController.h"
#include "app/render/NativeRenderAttachmentLease.h"
#include "platform/android/AndroidRenderAssetsLease.h"

namespace {

class NativeRetiredControllerHandle final {
public:
    explicit NativeRetiredControllerHandle(
            std::shared_ptr<AndroidRenderAssetsLease> assetsLease)
            : mAssetsLease(std::move(assetsLease)) {}

    void installController(
            std::shared_ptr<NativeCellularityController> controller) noexcept {
        mController = std::move(controller);
    }

    void close() noexcept {
        mController.reset();
        mAssetsLease.reset();
    }

private:
    std::shared_ptr<NativeCellularityController> mController;
    std::shared_ptr<AndroidRenderAssetsLease> mAssetsLease;
};

NativeControllerHandle* handleFromJlong(jlong value) noexcept {
    return reinterpret_cast<NativeControllerHandle*>(
            static_cast<std::uintptr_t>(value));
}

jlong handleToJlong(NativeControllerHandle* value) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(value));
}

NativeRetiredControllerHandle* retiredHandleFromJlong(jlong value) noexcept {
    return reinterpret_cast<NativeRetiredControllerHandle*>(
            static_cast<std::uintptr_t>(value));
}

jlong retiredHandleToJlong(NativeRetiredControllerHandle* value) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(value));
}

NativeControllerHandle* requireHandle(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = handleFromJlong(value);
    if (handle == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "NativeCellularityController handle is null");
    }
    return handle;
}

std::shared_ptr<NativeCellularityController> consumeControllerHandle(
        NativeControllerHandle* handle) {
    auto lease = handle->closeAndTakeLease();
    if (lease == nullptr) {
        throw std::logic_error("Native controller handle is already consumed");
    }
    const auto closeStatus = lease->beginAttachmentCloseAndDrain();
    if (closeStatus != cellularity::app::LeaseMutationStatus::Applied
            && closeStatus
                    != cellularity::app::LeaseMutationStatus::AttachmentClosing
            && closeStatus
                    != cellularity::app::LeaseMutationStatus::GenerationExhausted) {
        throw std::logic_error(
                "Prepared native controller attachment could not close");
    }
    auto taken = lease->takeControllerForAttachmentClose();
    if (taken.status != cellularity::app::LeaseMutationStatus::Applied
            || !taken.value.has_value() || *taken.value == nullptr) {
        throw std::logic_error(
                "Prepared native controller could not be consumed");
    }
    return std::move(*taken.value);
}

} // namespace

NativeControllerHandle::NativeControllerHandle(
        std::shared_ptr<NativeCellularityController> controller)
        : mLease(
                  std::make_shared<
                          cellularity::app::NativeRenderAttachmentLease>(
                          std::move(controller))) {
}

std::shared_ptr<NativeCellularityController>
NativeControllerHandle::acquireControllerShared() const {
    std::shared_ptr<cellularity::app::NativeRenderAttachmentLease> lease;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mClosed) {
            return nullptr;
        }
        lease = mLease;
    }
    return lease == nullptr ? nullptr : lease->acquireControllerForColdCall();
}

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
NativeControllerHandle::acquireRenderAttachmentLease() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed ? nullptr : mLease;
}

void NativeControllerHandle::installRenderAssetsLease(
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease) {
    if (assetsLease == nullptr) {
        throw std::invalid_argument("Native render assets lease is null");
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        throw std::logic_error("Native controller handle is closed");
    }
    if (mAssetsLease != nullptr && mAssetsLease != assetsLease) {
        throw std::logic_error(
                "Native controller already has a different render assets lease");
    }
    mAssetsLease = std::move(assetsLease);
}

std::shared_ptr<AndroidRenderAssetsLease>
NativeControllerHandle::acquireRenderAssetsLease() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed ? nullptr : mAssetsLease;
}

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
NativeControllerHandle::closeAndTakeLease() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        return nullptr;
    }
    mClosed = true;
    mAssetsLease.reset();
    return std::move(mLease);
}

jlong createNativeControllerHandle(
        std::shared_ptr<NativeCellularityController> controller) {
    if (controller == nullptr) {
        throw std::invalid_argument(
                "Native controller handle requires a controller");
    }
    return handleToJlong(new NativeControllerHandle(std::move(controller)));
}

std::shared_ptr<NativeCellularityController>
controllerSharedFromHandle(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle == nullptr) {
        return nullptr;
    }
    auto controller = handle->acquireControllerShared();
    if (controller == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "NativeCellularityController handle is closed");
    }
    return controller;
}

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
renderAttachmentLeaseFromHandle(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle == nullptr) {
        return nullptr;
    }
    auto lease = handle->acquireRenderAttachmentLease();
    if (lease == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Native render attachment lease is closed");
    }
    return lease;
}

void installRenderAssetsLeaseOnControllerHandle(JNIEnv* env,
        jlong value,
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle != nullptr) {
        handle->installRenderAssetsLease(std::move(assetsLease));
    }
}

std::shared_ptr<AndroidRenderAssetsLease>
renderAssetsLeaseFromControllerHandle(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle == nullptr) {
        return nullptr;
    }
    auto lease = handle->acquireRenderAssetsLease();
    if (lease == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Native controller render assets lease is unavailable");
    }
    return lease;
}

std::shared_ptr<NativeCellularityController>
closeAndDestroyNativeControllerHandle(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle == nullptr) {
        return nullptr;
    }
    auto assetsLease = handle->acquireRenderAssetsLease();
    (void)assetsLease;
    auto lease = handle->closeAndTakeLease();
    if (lease == nullptr) {
        delete handle;
        return nullptr;
    }
    const auto closeStatus = lease->beginAttachmentCloseAndDrain();
    if (closeStatus != cellularity::app::LeaseMutationStatus::Applied
            && closeStatus
                    != cellularity::app::LeaseMutationStatus::AttachmentClosing
            && closeStatus
                    != cellularity::app::LeaseMutationStatus::GenerationExhausted) {
        delete handle;
        throw std::logic_error(
                "Native render attachment lease could not close");
    }
    auto taken = lease->takeControllerForAttachmentClose();
    delete handle;
    if (taken.status != cellularity::app::LeaseMutationStatus::Applied
            || !taken.value.has_value()) {
        return nullptr;
    }
    return std::move(*taken.value);
}

jlong nativeControllerGenerationFromHandle(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle == nullptr) {
        return 0;
    }
    auto lease = handle->acquireRenderAttachmentLease();
    if (lease == nullptr) {
        throw std::logic_error("Native controller handle is closed");
    }
    return static_cast<jlong>(lease->generationSnapshot().controllerGeneration);
}

jlong commitNativeControllerReplacement(JNIEnv* env,
        jlong liveValue,
        jlong candidateValue,
        std::uint64_t expectedControllerGeneration) {
    if (liveValue == candidateValue) {
        throw std::invalid_argument(
                "Live and candidate controller handles must be different");
    }
    NativeControllerHandle* candidateHandle = requireHandle(env, candidateValue);
    if (candidateHandle == nullptr) {
        return 0;
    }
    std::unique_ptr<NativeControllerHandle> candidateOwner(candidateHandle);
    NativeControllerHandle* liveHandle = requireHandle(env, liveValue);
    if (liveHandle == nullptr) {
        return 0;
    }

    // Java transfers candidate ownership before entering this function. Delete
    // its wrapper on every path, while retaining both the controller and assets
    // as ordinary shared owners for the transaction.
    auto candidateAssets = candidateHandle->acquireRenderAssetsLease();
    (void)candidateAssets; // Keep the global AssetManager lease through commit.
    std::shared_ptr<NativeCellularityController> candidate;
    candidate = consumeControllerHandle(candidateHandle);
    candidateOwner.reset();

    auto liveLease = liveHandle->acquireRenderAttachmentLease();
    if (liveLease == nullptr) {
        throw std::logic_error("Live native controller handle is closed");
    }
    auto retiredHandle = std::make_unique<NativeRetiredControllerHandle>(
            liveHandle->acquireRenderAssetsLease());

    auto begin = liveLease->beginControllerRebindAndDrain(
            expectedControllerGeneration);
    if (begin.status != cellularity::app::LeaseMutationStatus::Applied
            || !begin.value.has_value()) {
        throw std::logic_error(
                "Native controller replacement generation is stale");
    }
    auto ticket = std::move(*begin.value);

    try {
        auto current = liveLease->acquireControllerForColdCall();
        if (current == nullptr) {
            throw std::logic_error(
                    "Live controller disappeared during replacement");
        }
        const auto configuration =
                current->microscopeRuntimeStartConfiguration();
        const auto viewport = current->viewportState();
        const auto logicalSurface = liveLease->logicalSurfaceState();
        const bool runtimeActive = current->microscopeRuntimeActive();

        if (logicalSurface.active) {
            candidate->setViewportSurfaceSize(
                    logicalSurface.width, logicalSurface.height);
        }
        candidate->setViewportCamera(
                viewport[0], viewport[1], viewport[2]);
        if (runtimeActive) {
            candidate->startMicroscopeRuntime(configuration);
        } else {
            candidate->setMicroscopeTemperatureMode(
                    configuration.temperatureMode);
            candidate->setMicroscopeToolOrdinal(configuration.toolOrdinal);
            candidate->setMicroscopeSlowMotionSpeed(
                    configuration.slowMotionSpeed);
            candidate->setMicroscopeIncubateTargetUps(
                    configuration.incubateTargetUps);
            candidate->setMicroscopePerformanceHudEnabled(
                    configuration.performanceHudEnabled);
        }

        // This drops controller-owned GLES names without issuing GL calls.
        // Vulkan adapters do not own the physical session and treat abandon as
        // a no-op. The next GLES frame lazily initializes the new generation.
        current->abandonRendererResources();

        std::shared_ptr<NativeCellularityController> retiredController;
        auto committed = liveLease->commitControllerRebind(
                std::move(ticket), std::move(candidate), retiredController);
        if (committed.status != cellularity::app::LeaseMutationStatus::Applied
                || !committed.value.has_value()
                || retiredController == nullptr) {
            throw std::logic_error(
                    "Native controller replacement could not be published");
        }
        retiredHandle->installController(std::move(retiredController));
        return retiredHandleToJlong(retiredHandle.release());
    } catch (...) {
        if (ticket.valid()) {
            const auto abortStatus =
                    liveLease->abortControllerRebind(std::move(ticket));
            if (abortStatus != cellularity::app::LeaseMutationStatus::Applied
                    && abortStatus
                            != cellularity::app::LeaseMutationStatus::
                                    AttachmentClosing) {
                // The original exception remains the useful failure. A failed
                // abort leaves no safe accepting state and is covered by the
                // lease's terminal invariant tests.
            }
        }
        throw;
    }
}

jlong detachNativeControllerHandleForClose(JNIEnv* env, jlong value) {
    NativeControllerHandle* handle = requireHandle(env, value);
    if (handle == nullptr) {
        return 0;
    }
    // Managed code transfers this wrapper before entering JNI. Own it from
    // this point so even allocation or invariant failures cannot strand a raw
    // handle that Java has already forgotten.
    std::unique_ptr<NativeControllerHandle> handleOwner(handle);
    auto retiredHandle = std::make_unique<NativeRetiredControllerHandle>(
            handle->acquireRenderAssetsLease());
    auto controller = consumeControllerHandle(handle);
    handleOwner.reset();
    controller->abandonRendererResources();
    retiredHandle->installController(std::move(controller));
    return retiredHandleToJlong(retiredHandle.release());
}

void closeAndDestroyNativeRetiredControllerHandle(jlong value) noexcept {
    NativeRetiredControllerHandle* handle = retiredHandleFromJlong(value);
    if (handle == nullptr) {
        return;
    }
    handle->close();
    delete handle;
}
