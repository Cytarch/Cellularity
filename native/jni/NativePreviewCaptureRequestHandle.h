#pragma once

#include <jni.h>

#include <memory>
#include <mutex>

#include "app/render/preview/NativePreviewCaptureRequestState.h"

struct NativePreviewCaptureRequestHandle final {
    std::mutex mutex;
    std::shared_ptr<cellularity::app::preview::PreviewCaptureRequestState>
            request;
    cellularity::app::preview::PreviewCaptureRequestKind kind =
            cellularity::app::preview::PreviewCaptureRequestKind::Genome;
    bool closed = false;
};

[[nodiscard]] NativePreviewCaptureRequestHandle*
previewCaptureRequestHandleFromJlong(jlong value) noexcept;

[[nodiscard]] jlong previewCaptureRequestHandleToJlong(
        NativePreviewCaptureRequestHandle* value) noexcept;
