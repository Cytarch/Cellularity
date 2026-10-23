#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <limits>

#include "app/render/NativeRenderProfilingLayout.h"
#include "app/render/NativeRenderProfilingState.h"

namespace {

bool writableDirectBuffer(JNIEnv* env, jobject buffer,
        std::uint8_t*& destination) noexcept {
    destination = nullptr;
    if (buffer == nullptr)
        return false;
    void* address = env->GetDirectBufferAddress(buffer);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (address == nullptr || capacity < 0
            || static_cast<std::uint64_t>(capacity)
                    < cellularity::app::render_worker::
                            kNativeRenderProfilingSize) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }
    jclass type = env->GetObjectClass(buffer);
    if (type == nullptr) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }
    const jmethodID isReadOnly = env->GetMethodID(type, "isReadOnly", "()Z");
    env->DeleteLocalRef(type);
    if (isReadOnly == nullptr) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }
    const jboolean readOnly = env->CallBooleanMethod(buffer, isReadOnly);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (readOnly == JNI_TRUE)
        return false;
    destination = static_cast<std::uint8_t*>(address);
    return true;
}

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_NativeRenderProfilingBridge_nativePublishRenderProfilingConfiguration(
        JNIEnv*, jclass, jboolean enabled, jlong resetGeneration) {
    using namespace cellularity::app::render_worker;
    if (resetGeneration < 0) {
        return static_cast<jint>(
                ProfilingConfigurationPublishResult::InvalidGeneration);
    }
    return static_cast<jint>(publishNativeRenderProfilingConfiguration(
            enabled == JNI_TRUE,
            static_cast<std::uint64_t>(resetGeneration)));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_NativeRenderProfilingBridge_nativeReadRenderProfilingSnapshot(
        JNIEnv* env, jclass, jobject buffer) {
    using namespace cellularity::app::render_worker;
    std::uint8_t* destination = nullptr;
    if (!writableDirectBuffer(env, buffer, destination)) {
        return static_cast<jint>(
                ProfilingSnapshotReadResult::InvalidBuffer);
    }
    if (!nativeRenderProfilingAvailable()) {
        return static_cast<jint>(ProfilingSnapshotReadResult::Unavailable);
    }
    NativeRenderProfilingSnapshot snapshot;
    if (!snapshotNativeRenderProfiling(snapshot)) {
        return static_cast<jint>(ProfilingSnapshotReadResult::Unavailable);
    }
    encodeNativeRenderProfilingSnapshot(snapshot, destination);
    return static_cast<jint>(ProfilingSnapshotReadResult::Ready);
}
