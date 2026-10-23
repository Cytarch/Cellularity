#include "AndroidRenderAssetsLeaseHandle.h"

#ifdef __ANDROID__

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "NativeJniUtils.h"
#include "platform/android/AndroidRenderAssetsLease.h"

namespace {

AndroidRenderAssetsLeaseHandle* handleFromJlong(jlong value) noexcept {
    return reinterpret_cast<AndroidRenderAssetsLeaseHandle*>(
            static_cast<std::uintptr_t>(value));
}

jlong handleToJlong(AndroidRenderAssetsLeaseHandle* value) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(value));
}

} // namespace

AndroidRenderAssetsLeaseHandle::AndroidRenderAssetsLeaseHandle(
        std::shared_ptr<AndroidRenderAssetsLease> lease)
        : mLease(std::move(lease)) {
    if (mLease == nullptr) {
        throw std::invalid_argument(
                "Android render assets handle requires a lease");
    }
}

std::shared_ptr<AndroidRenderAssetsLease>
AndroidRenderAssetsLeaseHandle::acquireShared() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed ? nullptr : mLease;
}

std::shared_ptr<AndroidRenderAssetsLease>
AndroidRenderAssetsLeaseHandle::closeAndTake() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed) {
        return nullptr;
    }
    mClosed = true;
    return std::move(mLease);
}

jlong createAndroidRenderAssetsLeaseHandle(
        std::shared_ptr<AndroidRenderAssetsLease> lease) {
    return handleToJlong(
            new AndroidRenderAssetsLeaseHandle(std::move(lease)));
}

std::shared_ptr<AndroidRenderAssetsLease>
androidRenderAssetsLeaseSharedFromHandle(JNIEnv* env, jlong value) {
    AndroidRenderAssetsLeaseHandle* handle = handleFromJlong(value);
    if (handle == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Android render assets lease handle is null");
        return nullptr;
    }
    auto lease = handle->acquireShared();
    if (lease == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Android render assets lease handle is closed");
    }
    return lease;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_platform_render_AndroidRenderAssetsOwner_nativeCreateLeaseHandle(
        JNIEnv* env, jclass, jobject assetManager) {
    try {
        return createAndroidRenderAssetsLeaseHandle(
                AndroidRenderAssetsLease::create(*env, assetManager));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_AndroidRenderAssetsOwner_nativeCloseAndDestroyLeaseHandle(
        JNIEnv*, jclass, jlong value) {
    AndroidRenderAssetsLeaseHandle* handle = handleFromJlong(value);
    if (handle == nullptr) {
        return;
    }
    auto lease = handle->closeAndTake();
    delete handle;
    lease.reset();
}

#endif
