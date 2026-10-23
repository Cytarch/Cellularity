#include "NativePreviewCaptureRequestHandle.h"

#include <cstdint>

#include "NativeJniUtils.h"
#include "app/render/preview/NativePreviewCaptureLayouts.h"

NativePreviewCaptureRequestHandle*
previewCaptureRequestHandleFromJlong(jlong value) noexcept {
    return reinterpret_cast<NativePreviewCaptureRequestHandle*>(
            static_cast<std::uintptr_t>(value));
}

jlong previewCaptureRequestHandleToJlong(
        NativePreviewCaptureRequestHandle* value) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(value));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewCaptureBridge_nativeCancel(
        JNIEnv* env, jclass, jlong requestHandle) {
    using cellularity::app::preview::PreviewCaptureCancelResult;
    NativePreviewCaptureRequestHandle* handle =
            previewCaptureRequestHandleFromJlong(requestHandle);
    if (handle == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Preview capture request handle is null");
        return static_cast<jint>(PreviewCaptureCancelResult::AlreadyTerminal);
    }
    std::lock_guard<std::mutex> lock(handle->mutex);
    if (handle->closed || handle->request == nullptr) {
        return static_cast<jint>(PreviewCaptureCancelResult::AlreadyTerminal);
    }
    return static_cast<jint>(handle->request->cancel());
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewCaptureBridge_nativeDestroy(
        JNIEnv*, jclass, jlong requestHandle) {
    NativePreviewCaptureRequestHandle* handle =
            previewCaptureRequestHandleFromJlong(requestHandle);
    if (handle == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (!handle->closed) {
            handle->closed = true;
            if (handle->request != nullptr) {
                (void)handle->request->cancel();
                handle->request.reset();
            }
        }
    }
    delete handle;
}
