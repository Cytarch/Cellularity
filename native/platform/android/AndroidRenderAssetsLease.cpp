#include "platform/android/AndroidRenderAssetsLease.h"

#ifdef __ANDROID__

#include <android/asset_manager_jni.h>

#include <stdexcept>

std::shared_ptr<AndroidRenderAssetsLease> AndroidRenderAssetsLease::create(
        JNIEnv& env,
        jobject assetManager) {
    if (assetManager == nullptr) {
        throw std::invalid_argument(
                "Android render assets require a Java AssetManager");
    }
    JavaVM* javaVm = nullptr;
    if (env.GetJavaVM(&javaVm) != JNI_OK || javaVm == nullptr) {
        throw std::runtime_error(
                "Android render assets could not resolve the Java VM");
    }
    jobject global = env.NewGlobalRef(assetManager);
    if (global == nullptr) {
        throw std::bad_alloc();
    }
    AAssetManager* native = AAssetManager_fromJava(&env, global);
    if (native == nullptr) {
        env.DeleteGlobalRef(global);
        throw std::runtime_error(
                "Android render assets could not resolve AAssetManager");
    }
    return std::shared_ptr<AndroidRenderAssetsLease>(
            new AndroidRenderAssetsLease(javaVm, global, native));
}

AndroidRenderAssetsLease::AndroidRenderAssetsLease(JavaVM* javaVm,
        jobject assetManagerGlobal,
        AAssetManager* assetManager) noexcept
        : mJavaVm(javaVm),
          mAssetManagerGlobal(assetManagerGlobal),
          mAssetManager(assetManager) {
}

AndroidRenderAssetsLease::~AndroidRenderAssetsLease() {
    JavaVM* javaVm = nullptr;
    jobject global = nullptr;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mClosed = true;
        javaVm = mJavaVm;
        global = mAssetManagerGlobal;
        mJavaVm = nullptr;
        mAssetManagerGlobal = nullptr;
        mAssetManager = nullptr;
    }
    if (javaVm == nullptr || global == nullptr) {
        return;
    }

    JNIEnv* env = nullptr;
    bool detachAfterDelete = false;
    const jint environmentStatus = javaVm->GetEnv(
            reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (environmentStatus == JNI_EDETACHED) {
        if (javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK
                || env == nullptr) {
            return;
        }
        detachAfterDelete = true;
    } else if (environmentStatus != JNI_OK || env == nullptr) {
        return;
    }
    env->DeleteGlobalRef(global);
    if (detachAfterDelete) {
        javaVm->DetachCurrentThread();
    }
}

AndroidRenderAssets AndroidRenderAssetsLease::assets() const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosed || mAssetManagerGlobal == nullptr || mAssetManager == nullptr) {
        throw std::logic_error("Android render assets lease is closed");
    }
    return AndroidRenderAssets(mAssetManager);
}

bool AndroidRenderAssetsLease::closed() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed;
}

#endif
