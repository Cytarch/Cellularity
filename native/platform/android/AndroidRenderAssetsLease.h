#pragma once

#ifdef __ANDROID__

#include <jni.h>

#include <memory>
#include <mutex>

#include "platform/android/AndroidRenderAssets.h"

class AndroidRenderAssetsLease final {
public:
    [[nodiscard]] static std::shared_ptr<AndroidRenderAssetsLease> create(
            JNIEnv& env,
            jobject assetManager);

    AndroidRenderAssetsLease(const AndroidRenderAssetsLease&) = delete;
    AndroidRenderAssetsLease& operator=(const AndroidRenderAssetsLease&) = delete;
    ~AndroidRenderAssetsLease();

    [[nodiscard]] AndroidRenderAssets assets() const;
    [[nodiscard]] bool closed() const noexcept;

private:
    AndroidRenderAssetsLease(JavaVM* javaVm,
            jobject assetManagerGlobal,
            AAssetManager* assetManager) noexcept;

    mutable std::mutex mMutex;
    JavaVM* mJavaVm = nullptr;
    jobject mAssetManagerGlobal = nullptr;
    AAssetManager* mAssetManager = nullptr;
    bool mClosed = false;
};

#endif
