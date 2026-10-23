#pragma once

#include <jni.h>

#include <memory>
#include <mutex>

class NativeCellularityController;
class AndroidRenderAssetsLease;

namespace cellularity::app {
class NativeRenderAttachmentLease;
} // namespace cellularity::app

class NativeControllerHandle final {
public:
    explicit NativeControllerHandle(
            std::shared_ptr<NativeCellularityController> controller);

    NativeControllerHandle(const NativeControllerHandle&) = delete;
    NativeControllerHandle& operator=(const NativeControllerHandle&) = delete;

    [[nodiscard]] std::shared_ptr<NativeCellularityController>
    acquireControllerShared() const;
    [[nodiscard]] std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
    acquireRenderAttachmentLease() const;
    void installRenderAssetsLease(
            std::shared_ptr<AndroidRenderAssetsLease> assetsLease);
    [[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease>
    acquireRenderAssetsLease() const;
    [[nodiscard]] std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
    closeAndTakeLease();

private:
    mutable std::mutex mMutex;
    std::shared_ptr<cellularity::app::NativeRenderAttachmentLease> mLease;
    std::shared_ptr<AndroidRenderAssetsLease> mAssetsLease;
    bool mClosed = false;
};

[[nodiscard]] jlong createNativeControllerHandle(
        std::shared_ptr<NativeCellularityController> controller);

[[nodiscard]] std::shared_ptr<NativeCellularityController>
controllerSharedFromHandle(JNIEnv* env, jlong handle);

[[nodiscard]] std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
renderAttachmentLeaseFromHandle(JNIEnv* env, jlong handle);

void installRenderAssetsLeaseOnControllerHandle(JNIEnv* env,
        jlong handle,
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease);

[[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease>
renderAssetsLeaseFromControllerHandle(JNIEnv* env, jlong handle);

[[nodiscard]] std::shared_ptr<NativeCellularityController>
closeAndDestroyNativeControllerHandle(JNIEnv* env, jlong handle);

[[nodiscard]] jlong nativeControllerGenerationFromHandle(
        JNIEnv* env, jlong handle);

// Consumes candidateHandle on every path. On success the live handle keeps its
// durable attachment lease and the returned retirement handle owns the old
// controller until managed code closes it outside gateway locks.
[[nodiscard]] jlong commitNativeControllerReplacement(JNIEnv* env,
        jlong liveHandle,
        jlong candidateHandle,
        std::uint64_t expectedControllerGeneration);

// Consumes and deletes the live wrapper after its attachment lease drains.
// The returned retirement handle performs the potentially blocking controller
// destruction later, outside the gateway commit barrier.
[[nodiscard]] jlong detachNativeControllerHandleForClose(
        JNIEnv* env, jlong handle);

void closeAndDestroyNativeRetiredControllerHandle(jlong handle) noexcept;
