#pragma once

#ifdef __ANDROID__

#include <jni.h>

#include <memory>
#include <mutex>

class AndroidRenderAssetsLease;

class AndroidRenderAssetsLeaseHandle final {
public:
    explicit AndroidRenderAssetsLeaseHandle(
            std::shared_ptr<AndroidRenderAssetsLease> lease);

    [[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease> acquireShared() const;
    [[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease> closeAndTake();

private:
    mutable std::mutex mMutex;
    std::shared_ptr<AndroidRenderAssetsLease> mLease;
    bool mClosed = false;
};

[[nodiscard]] jlong createAndroidRenderAssetsLeaseHandle(
        std::shared_ptr<AndroidRenderAssetsLease> lease);

[[nodiscard]] std::shared_ptr<AndroidRenderAssetsLease>
androidRenderAssetsLeaseSharedFromHandle(JNIEnv* env, jlong handle);

#endif
