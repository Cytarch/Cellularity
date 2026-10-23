#include <jni.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>

#include "AndroidJniRenderBridge.h"
#include "AndroidRenderAssetsLeaseHandle.h"
#include "NativeControllerHandle.h"
#include "NativeJniBridgeState.h"
#include "NativeJniUtils.h"
#include "NativePreviewEndpointHandle.h"
#include "NativeRenderEndpointHandle.h"
#include "app/EnvironmentFieldPreviewEndpoint.h"
#include "app/NativeCellularityController.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "app/genome_preview/GenomePreviewEndpointLayouts.h"
#include "app/render/NativeRenderAttachmentLease.h"
#include "core/GenomeIdentity.h"
#include "formats/GenomeDraftCodec.h"
#include "render/backend/RenderSurfaceSession.h"

namespace {

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
renderEndpointLeaseFromHandle(JNIEnv*, jlong endpointHandle) {
    return ::renderEndpointLeaseFromHandle(
            static_cast<std::uint64_t>(endpointHandle));
}

std::shared_ptr<AndroidRenderAssetsLease> renderEndpointAssetsFromHandle(
        JNIEnv*, jlong endpointHandle) {
    return ::renderEndpointAssetsFromHandle(
            static_cast<std::uint64_t>(endpointHandle));
}

jlong packedFrameResult(
        const cellularity::render::RenderFrameResult& result) noexcept {
    return static_cast<jlong>(
            cellularity::render::packRenderFrameResultSigned(result));
}

jlong invalidFrameResult() noexcept {
    return static_cast<jlong>(cellularity::render::kInvalidRenderFrameWire);
}

jlong skippedFrameResult() noexcept {
    return packedFrameResult(cellularity::render::makeRenderFrameResult(
            cellularity::render::RenderFrameOutcome::SkippedNotReady));
}

std::uint8_t* requireEnvironmentFieldPreviewResultBuffer(
        JNIEnv* env, jobject resultBuffer) {
    if (resultBuffer == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION,
                "Environment Field Preview result buffer is null");
        return nullptr;
    }
    void* address = env->GetDirectBufferAddress(resultBuffer);
    const jlong capacity = env->GetDirectBufferCapacity(resultBuffer);
    if (address == nullptr
            || capacity
                    < cellularity::app::kEnvironmentFieldPreviewResultSize) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION,
                "Environment Field Preview result buffer must be direct and at least 24 bytes");
        return nullptr;
    }
    return static_cast<std::uint8_t*>(address);
}

std::uint8_t* requireDirectResultBuffer(
        JNIEnv* env,
        jobject resultBuffer,
        std::size_t requiredBytes,
        const char* message) {
    if (resultBuffer == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, message);
        return nullptr;
    }
    void* address = env->GetDirectBufferAddress(resultBuffer);
    const jlong capacity = env->GetDirectBufferCapacity(resultBuffer);
    if (address == nullptr
            || capacity < 0
            || static_cast<std::uint64_t>(capacity) < requiredBytes) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, message);
        return nullptr;
    }
    return static_cast<std::uint8_t*>(address);
}

bool hasGenomeDraftHeader(JNIEnv* env, jbyteArray genomeBytes) {
    if (genomeBytes == nullptr
            || env->GetArrayLength(genomeBytes)
                    < static_cast<jsize>(cellularity::kGenomeDraftWireHeaderBytes)) {
        return false;
    }
    std::array<jbyte, 8> header{};
    env->GetByteArrayRegion(
            genomeBytes, 0, static_cast<jsize>(header.size()), header.data());
    return !env->ExceptionCheck()
            && static_cast<std::uint8_t>(header[0]) == 0x43U
            && static_cast<std::uint8_t>(header[1]) == 0x47U
            && static_cast<std::uint8_t>(header[2]) == 0x44U
            && static_cast<std::uint8_t>(header[3]) == 0x57U
            && static_cast<std::uint8_t>(header[4]) == 0U
            && static_cast<std::uint8_t>(header[5]) == 0U
            && static_cast<std::uint8_t>(header[6]) == 0U
            && static_cast<std::uint8_t>(header[7]) == 2U;
}

void ensureGlesResourcesForFrame(JNIEnv* env,
        jlong endpointHandle,
        cellularity::app::NativeRenderAttachmentLease& lease,
        cellularity::app::FrameGuard& guard) {
    NativeCellularityController& controller = guard.controller();
    if (controller.hasRenderSurfaceSession()
            && controller.renderBackendKind()
                    != cellularity::render::RenderBackendKind::OpenGlEs) {
        return;
    }
    const std::uint64_t controllerGeneration =
            guard.key().controllerGeneration;
    if (lease.hasGlesResourcesForFrame(guard)) {
        return;
    }
    auto assetsLease = renderEndpointAssetsFromHandle(env, endpointHandle);
    if (assetsLease == nullptr) {
        throw std::logic_error(
                "GLES frame requires a live Android render assets lease");
    }
    loadAndroidRendererResources(controller, *assetsLease);
    if (lease.markGlesResourcesLoaded(controllerGeneration)
            != cellularity::app::LeaseMutationStatus::Applied) {
        throw std::logic_error(
                "GLES resources were loaded for a stale controller generation");
    }
}

std::shared_ptr<NativeCellularityController> controllerFromRenderEndpoint(
        JNIEnv* env, jlong endpointHandle) {
    auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
    if (lease == nullptr) {
        return nullptr;
    }
    return lease->acquireControllerForColdCall();
}

} // namespace

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewEndpointOwner_nativeCreateEndpointHandle(
        JNIEnv* env, jclass, jlong controllerHandle, jint kind) {
    try {
        auto controller = controllerSharedFromHandle(env, controllerHandle);
        if (controller == nullptr) {
            return nullptr;
        }
        std::uint64_t endpointHandle = 0;
        std::uint64_t generation = 0;
        switch (kind) {
            case static_cast<jint>(PreviewEndpointKind::GenomePreview): {
                auto endpoint = controller->genomePreviewEndpoint();
                if (endpoint == nullptr) {
                    return nullptr;
                }
                generation = endpoint->generation();
                endpointHandle = createNativePreviewEndpointHandle(
                        std::move(endpoint), controller);
                break;
            }
            case static_cast<jint>(
                    PreviewEndpointKind::EnvironmentFieldPreview): {
                auto endpoint = controller->environmentFieldPreviewEndpoint();
                if (endpoint == nullptr) {
                    return nullptr;
                }
                generation = endpoint->generation();
                endpointHandle = createNativePreviewEndpointHandle(
                        std::move(endpoint), controller);
                break;
            }
            default:
                throw std::invalid_argument(
                        "Native preview endpoint kind is invalid");
        }
        const jlong values[] = {
                static_cast<jlong>(endpointHandle),
                static_cast<jlong>(generation),
        };
        jlongArray result = env->NewLongArray(2);
        if (result == nullptr) {
            closeAndDestroyNativePreviewEndpointHandle(endpointHandle);
            return nullptr;
        }
        env->SetLongArrayRegion(result, 0, 2, values);
        if (env->ExceptionCheck()) {
            closeAndDestroyNativePreviewEndpointHandle(endpointHandle);
            return nullptr;
        }
        return result;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewEndpointOwner_nativeCloseAndDestroyEndpointHandle(
        JNIEnv*, jclass, jlong handle) {
    closeAndDestroyNativePreviewEndpointHandle(
            static_cast<std::uint64_t>(handle));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_platform_render_microscope_NativeRenderEndpointOwner_nativeCreateEndpointHandle(
        JNIEnv* env, jclass, jlong controllerHandle) {
    try {
        auto attachmentLease = renderAttachmentLeaseFromHandle(
                env, controllerHandle);
        auto assetsLease = renderAssetsLeaseFromControllerHandle(
                env, controllerHandle);
        if (attachmentLease == nullptr || assetsLease == nullptr) {
            return 0;
        }
        return static_cast<jlong>(createNativeRenderEndpointHandle(
                std::move(attachmentLease), std::move(assetsLease)));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_microscope_NativeRenderEndpointOwner_nativeCloseAndDestroyEndpointHandle(
        JNIEnv*, jclass, jlong endpointHandle) {
    closeAndDestroyNativeRenderEndpointHandle(endpointHandle);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeAttachRenderAssetsLease(
        JNIEnv* env, jclass, jlong handle, jlong assetsLeaseHandle) {
    try {
        auto assetsLease = androidRenderAssetsLeaseSharedFromHandle(
                env, assetsLeaseHandle);
        if (assetsLease == nullptr) {
            return;
        }
        installRenderAssetsLeaseOnControllerHandle(
                env, handle, std::move(assetsLease));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeAuthorizeRenderWorker(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken,
        jlong workerIdentity) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return 0;
        }
        const auto result = lease->authorizeWorker(
                static_cast<std::uint64_t>(surfaceToken),
                static_cast<std::uint64_t>(workerIdentity));
        if ((result.status != cellularity::app::LeaseMutationStatus::Applied
                    && result.status
                            != cellularity::app::LeaseMutationStatus::AlreadyAuthorized)
                || !result.value.has_value()) {
            throw std::logic_error("Native render worker authorization failed");
        }
        return static_cast<jlong>(result.value->bindingGeneration);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeActivateRenderSurface(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken,
        jlong workerIdentity, jlong workerBindingGeneration,
        jlong platformSurfaceGeneration, jint width, jint height) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return nullptr;
        }
        const cellularity::app::WorkerBindingKey worker{
                static_cast<std::uint64_t>(surfaceToken),
                static_cast<std::uint64_t>(workerIdentity),
                static_cast<std::uint64_t>(workerBindingGeneration)
        };
        const auto result = lease->activateSurface(worker,
                static_cast<std::uint64_t>(platformSurfaceGeneration), width,
                height);
        if (result.status != cellularity::app::LeaseMutationStatus::Applied
                || !result.value.has_value()) {
            throw std::logic_error("Native render surface activation failed");
        }
        auto controller = lease->acquireControllerForColdCall();
        if (controller == nullptr) {
            throw std::logic_error(
                    "Native render controller is unavailable after activation");
        }
        controller->setViewportSurfaceSize(width, height);
        controller->invalidateMicroscopeFrameData();
        const auto generation = lease->generationSnapshot();
        const jlong values[] = {
                static_cast<jlong>(result.value->backendBindingGeneration),
                static_cast<jlong>(generation.controllerGeneration),
                static_cast<jlong>(generation.inputEpoch),
                static_cast<jlong>(
                        generation.glesResourcesControllerGeneration),
        };
        jlongArray array = env->NewLongArray(4);
        if (array != nullptr) {
            env->SetLongArrayRegion(array, 0, 4, values);
        }
        return array;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeUpdateRenderSurfaceDimensions(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken, jint width,
        jint height) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return static_cast<jint>(
                    cellularity::app::LeaseMutationStatus::AttachmentClosing);
        }
        const auto status = lease->updateSurfaceDimensions(
                static_cast<std::uint64_t>(surfaceToken), width, height);
        if (status == cellularity::app::LeaseMutationStatus::Applied
                || status
                        == cellularity::app::LeaseMutationStatus::DeferredRebind) {
            auto controller = lease->acquireControllerForColdCall();
            if (controller != nullptr) {
                controller->setViewportSurfaceSize(width, height);
                controller->invalidateMicroscopeFrameData();
            }
        }
        return static_cast<jint>(status);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return static_cast<jint>(
                cellularity::app::LeaseMutationStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDeactivateRenderSurface(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return static_cast<jint>(
                    cellularity::app::LeaseMutationStatus::AttachmentClosing);
        }
        return static_cast<jint>(lease->deactivateSurface(
                static_cast<std::uint64_t>(surfaceToken)));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return static_cast<jint>(
                cellularity::app::LeaseMutationStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMarkRenderWorkerClosing(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken,
        jlong workerIdentity, jlong workerBindingGeneration) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return static_cast<jint>(
                    cellularity::app::LeaseMutationStatus::AttachmentClosing);
        }
        return static_cast<jint>(lease->markWorkerClosing(
                {static_cast<std::uint64_t>(surfaceToken),
                        static_cast<std::uint64_t>(workerIdentity),
                        static_cast<std::uint64_t>(
                                workerBindingGeneration)}));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return static_cast<jint>(
                cellularity::app::LeaseMutationStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeFinishRenderWorkerClose(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken,
        jlong workerIdentity, jlong workerBindingGeneration) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return static_cast<jint>(
                    cellularity::app::LeaseMutationStatus::AttachmentClosing);
        }
        return static_cast<jint>(lease->finishWorkerClose(
                {static_cast<std::uint64_t>(surfaceToken),
                        static_cast<std::uint64_t>(workerIdentity),
                        static_cast<std::uint64_t>(
                                workerBindingGeneration)}));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return static_cast<jint>(
                cellularity::app::LeaseMutationStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativePreparePreviewSurfaceResources(
        JNIEnv* env, jclass, jlong endpointHandle) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        auto assetsLease = renderEndpointAssetsFromHandle(env, endpointHandle);
        if (lease == nullptr || assetsLease == nullptr) {
            return;
        }
        auto controller = lease->acquireControllerForColdCall();
        if (controller == nullptr) {
            return;
        }
        loadAndroidRendererResources(*controller, *assetsLease);
        const auto generation = lease->generationSnapshot();
        if (lease->markGlesResourcesLoaded(generation.controllerGeneration)
                != cellularity::app::LeaseMutationStatus::Applied) {
            throw std::logic_error(
                    "Preview resources belong to a stale controller generation");
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSurfaceDestroyed(
        JNIEnv* env,
        jclass,
        jlong endpointHandle) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->abandonRendererResources();
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetViewportSurfaceSize(JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jint width,
        jint height) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setViewportSurfaceSize(width, height);
            controller->invalidateMicroscopeFrameData();
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetViewportCamera(JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jfloat cameraX,
        jfloat cameraY,
        jfloat zoom) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setViewportCamera(cameraX, cameraY, zoom);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMoveViewportByScreenDelta(JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jfloat dx,
        jfloat dy) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->moveViewportByScreenDelta(dx, dy);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeZoomViewportAt(JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jfloat factor,
        jfloat screenX,
        jfloat screenY) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->zoomViewportAt(factor, screenX, screenY);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeViewportState(JNIEnv* env,
        jclass,
        jlong endpointHandle) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        std::array<float, 7> state{};
        jfloatArray result = env->NewFloatArray(static_cast<jsize>(state.size()));
        if (result == nullptr) {
            return nullptr;
        }
        if (controller != nullptr) {
            state = controller->viewportState();
        }
        env->SetFloatArrayRegion(result, 0, static_cast<jsize>(state.size()), state.data());
        return result;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDrawGenomePreview(JNIEnv* env,
        jclass,
        jlong handle,
        jobject acknowledgmentBuffer) {
    std::uint8_t* acknowledgmentBytes = requireDirectResultBuffer(
            env,
            acknowledgmentBuffer,
            cellularity::app::genome_preview::kGenomePreviewAcknowledgmentSize,
            "Genome Preview acknowledgment buffer must be direct and at least 112 bytes");
    if (acknowledgmentBytes == nullptr || env->ExceptionCheck()) {
        return invalidFrameResult();
    }
    std::memset(acknowledgmentBytes, 0,
            cellularity::app::genome_preview::kGenomePreviewAcknowledgmentSize);
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return invalidFrameResult();
        }
        const auto result = controller->drawGenomePreview();
        const auto endpoint = controller->genomePreviewEndpoint();
        if (endpoint != nullptr) {
            const auto encoded = cellularity::app::genome_preview::encodeGenomePreviewAcknowledgment(endpoint->readAcknowledgment());
            std::memcpy(acknowledgmentBytes, encoded.data(), encoded.size());
        }
        return packedFrameResult(result);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return invalidFrameResult();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeReadGenomePreviewAcknowledgment(
        JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jlong endpointGeneration,
        jobject acknowledgmentBuffer) {
    std::uint8_t* acknowledgmentBytes = requireDirectResultBuffer(
            env,
            acknowledgmentBuffer,
            cellularity::app::genome_preview::kGenomePreviewAcknowledgmentSize,
            "Genome Preview acknowledgment buffer must be direct and at least 112 bytes");
    if (acknowledgmentBytes == nullptr || env->ExceptionCheck()) {
        return JNI_FALSE;
    }
    std::memset(acknowledgmentBytes, 0,
            cellularity::app::genome_preview::kGenomePreviewAcknowledgmentSize);
    try {
        const auto acquired = acquireGenomePreviewEndpoint(
                static_cast<std::uint64_t>(endpointHandle),
                static_cast<std::uint64_t>(endpointGeneration));
        if (acquired.endpoint == nullptr) {
            return JNI_FALSE;
        }
        const auto encoded =
                cellularity::app::genome_preview::encodeGenomePreviewAcknowledgment(
                        acquired.endpoint->readAcknowledgment());
        std::memcpy(acknowledgmentBytes, encoded.data(), encoded.size());
        return JNI_TRUE;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeResolveGenomePreviewEndpointGeneration(JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return 0;
        }
        const auto endpoint = controller->genomePreviewEndpoint();
        return endpoint != nullptr ? static_cast<jlong>(endpoint->generation()) : 0;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeGenomePreviewSurfaceCreated(JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerFromRenderEndpoint(env, handle);
        return controller != nullptr
                ? static_cast<jlong>(controller->genomePreviewSurfaceCreated())
                : 0;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeGenomePreviewSurfaceDestroyed(JNIEnv* env, jclass, jlong handle, jlong attachmentGeneration) {
    try {
        auto controller = controllerFromRenderEndpoint(env, handle);
        if (controller != nullptr && attachmentGeneration != 0) {
            controller->genomePreviewSurfaceDestroyed(
                    static_cast<std::uint64_t>(attachmentGeneration));
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSubstratePreviewSurfaceCreated(JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerFromRenderEndpoint(env, handle);
        return controller != nullptr
                ? static_cast<jlong>(controller->substratePreviewSurfaceCreated())
                : 0;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSubstratePreviewSurfaceDestroyed(JNIEnv* env, jclass, jlong handle, jlong attachmentGeneration) {
    try {
        auto controller = controllerFromRenderEndpoint(env, handle);
        if (controller != nullptr && attachmentGeneration != 0) {
            controller->substratePreviewSurfaceDestroyed(
                    static_cast<std::uint64_t>(attachmentGeneration));
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativePublishGenomePreviewContent(JNIEnv* env, jclass, jlong handle, jlong endpointGeneration, jbyteArray genomeBytes, jdouble simulationTimeHours, jlong producerSequence, jobject resultBuffer) {
    std::uint8_t* resultBytes = requireDirectResultBuffer(
            env,
            resultBuffer,
            cellularity::app::genome_preview::kGenomePreviewPublishResultSize,
            "Genome Preview publish result buffer must be direct and at least 24 bytes");
    if (resultBytes == nullptr || env->ExceptionCheck()) {
        return;
    }
    auto writeResult = [&](cellularity::app::genome_preview::GenomePreviewStatePublishResult result) {
        const auto encoded = cellularity::app::genome_preview::encodeGenomePreviewPublishResult(result);
        std::memcpy(resultBytes, encoded.data(), encoded.size());
    };
    std::memset(resultBytes, 0,
            cellularity::app::genome_preview::kGenomePreviewPublishResultSize);
    writeResult({cellularity::app::genome_preview::GenomePreviewStatePublishStatus::InvalidArgument, 0});
    if (!hasGenomeDraftHeader(env, genomeBytes) || env->ExceptionCheck()) {
        return;
    }
    const jsize length = env->GetArrayLength(genomeBytes);
    if (length <= 0
            || static_cast<std::size_t>(length)
                    > cellularity::kGenomeDraftMaximumWireBytes) {
        return;
    }
    jboolean copied = JNI_FALSE;
    jbyte* pinned = env->GetByteArrayElements(genomeBytes, &copied);
    if (pinned == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        writeResult({cellularity::app::genome_preview::GenomePreviewStatePublishStatus::InternalFailure, 0});
        return;
    }
    try {
        const auto acquired = acquireGenomePreviewEndpoint(
                static_cast<std::uint64_t>(handle),
                static_cast<std::uint64_t>(endpointGeneration));
        const auto endpoint = acquired.endpoint;
        const auto result = endpoint != nullptr
                ? endpoint->publishContent(
                          reinterpret_cast<const std::uint8_t*>(pinned),
                          static_cast<std::size_t>(length),
                          static_cast<double>(simulationTimeHours),
                          static_cast<std::uint64_t>(producerSequence),
                          static_cast<std::uint64_t>(endpointGeneration))
                : cellularity::app::genome_preview::GenomePreviewStatePublishResult{
                          cellularity::app::genome_preview::GenomePreviewStatePublishStatus::Closing,
                          0
                  };
        env->ReleaseByteArrayElements(genomeBytes, pinned, JNI_ABORT);
        writeResult(result);
    } catch (const std::bad_alloc&) {
        env->ReleaseByteArrayElements(genomeBytes, pinned, JNI_ABORT);
        writeResult({cellularity::app::genome_preview::GenomePreviewStatePublishStatus::InternalFailure, 0});
    } catch (const std::exception&) {
        env->ReleaseByteArrayElements(genomeBytes, pinned, JNI_ABORT);
        writeResult({cellularity::app::genome_preview::GenomePreviewStatePublishStatus::InvalidArgument, 0});
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativePublishGenomePreviewViewState(JNIEnv* env, jclass, jlong handle, jlong endpointGeneration, jint selectedGeneIndex, jboolean gridEnabled, jboolean treeEnabled, jint viewportWidth, jint viewportHeight, jlong producerSequence, jobject resultBuffer) {
    std::uint8_t* resultBytes = requireDirectResultBuffer(
            env,
            resultBuffer,
            cellularity::app::genome_preview::kGenomePreviewPublishResultSize,
            "Genome Preview publish result buffer must be direct and at least 24 bytes");
    if (resultBytes == nullptr || env->ExceptionCheck()) {
        return;
    }
    std::memset(resultBytes, 0,
            cellularity::app::genome_preview::kGenomePreviewPublishResultSize);
    const auto acquired = acquireGenomePreviewEndpoint(
            static_cast<std::uint64_t>(handle),
            static_cast<std::uint64_t>(endpointGeneration));
    const auto endpoint = acquired.endpoint;
    const cellularity::app::genome_preview::GenomePreviewViewUpdate update{
            static_cast<std::int32_t>(selectedGeneIndex),
            gridEnabled == JNI_TRUE,
            treeEnabled == JNI_TRUE,
            static_cast<std::int32_t>(viewportWidth),
            static_cast<std::int32_t>(viewportHeight)
    };
    const auto result = endpoint != nullptr
            ? endpoint->publishViewState(
                      update,
                      static_cast<std::uint64_t>(producerSequence),
                      static_cast<std::uint64_t>(endpointGeneration))
            : cellularity::app::genome_preview::GenomePreviewStatePublishResult{
                      cellularity::app::genome_preview::GenomePreviewStatePublishStatus::Closing,
                      0
              };
    const auto encoded = cellularity::app::genome_preview::encodeGenomePreviewPublishResult(result);
    std::memcpy(resultBytes, encoded.data(), encoded.size());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeEnqueueGenomePreviewInteraction(JNIEnv* env, jclass, jlong handle, jlong endpointGeneration, jlong attachmentGeneration, jlong completionToken, jint kind, jfloat first, jfloat second, jfloat third) {
    try {
        const auto acquired = acquireGenomePreviewEndpoint(
                static_cast<std::uint64_t>(handle),
                static_cast<std::uint64_t>(endpointGeneration));
        const auto endpoint = acquired.endpoint;
        if (endpoint == nullptr) {
            return static_cast<jint>(cellularity::app::genome_preview::GenomePreviewInteractionEnqueueStatus::Closing);
        }
        cellularity::app::genome_preview::GenomePreviewInteractionValue value;
        switch (kind) {
            case 0:
                value = cellularity::app::genome_preview::GenomePreviewRestoreViewport{first, second, third};
                break;
            case 1:
                value = cellularity::app::genome_preview::GenomePreviewMoveViewport{first, second};
                break;
            case 2:
                value = cellularity::app::genome_preview::GenomePreviewZoomViewport{first, second, third};
                break;
            case 3:
                value = cellularity::app::genome_preview::GenomePreviewSelectGene{first, second};
                break;
            case 4:
                value = cellularity::app::genome_preview::GenomePreviewCaptureGhost{};
                break;
            case 5:
                value = cellularity::app::genome_preview::GenomePreviewClearGhost{};
                break;
            default:
                return static_cast<jint>(cellularity::app::genome_preview::GenomePreviewInteractionEnqueueStatus::InvalidArgument);
        }
        const cellularity::app::genome_preview::GenomePreviewInteractionInput input{
                static_cast<std::uint64_t>(completionToken),
                static_cast<std::uint64_t>(endpointGeneration),
                static_cast<std::uint64_t>(attachmentGeneration),
                std::move(value)
        };
        return static_cast<jint>(endpoint->enqueueInteraction(
                                                 input, static_cast<std::uint64_t>(endpointGeneration))
                        .status);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return static_cast<jint>(cellularity::app::genome_preview::GenomePreviewInteractionEnqueueStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_preview_EnvironmentFieldPreviewBridge_nativePublishEnvironmentFieldPreviewState(
        JNIEnv* env, jclass, jlong handle, jlong endpointGeneration,
        jint target, jint fieldType, jdouble base, jdouble strength,
        jdouble angle, jdouble offset, jdouble softness,
        jobject resultBuffer) {
    std::uint8_t* resultBytes =
            requireEnvironmentFieldPreviewResultBuffer(env, resultBuffer);
    if (resultBytes == nullptr || env->ExceptionCheck()) {
        return;
    }
    std::memset(
            resultBytes,
            0,
            cellularity::app::kEnvironmentFieldPreviewResultSize);

    try {
        const cellularity::app::EnvironmentFieldPreviewUpdate update{
                static_cast<std::int32_t>(target),
                static_cast<std::int32_t>(fieldType),
                static_cast<double>(base),
                static_cast<double>(strength),
                static_cast<double>(angle),
                static_cast<double>(offset),
                static_cast<double>(softness),
        };
        const auto acquired = acquireEnvironmentFieldPreviewEndpoint(
                static_cast<std::uint64_t>(handle),
                static_cast<std::uint64_t>(endpointGeneration));
        const auto endpoint = acquired.endpoint;
        const cellularity::app::EnvironmentFieldPreviewUpdateResult result =
                endpoint != nullptr
                ? endpoint->publish(
                          static_cast<std::uint64_t>(endpointGeneration), update)
                : cellularity::app::EnvironmentFieldPreviewUpdateResult{
                          cellularity::app::EnvironmentFieldPreviewUpdateStatus::Closing,
                          0
                  };
        const auto encoded =
                cellularity::app::encodeEnvironmentFieldPreviewUpdateResult(
                        result);
        std::memcpy(resultBytes, encoded.data(), encoded.size());
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDrawEnvironmentFieldPreview(JNIEnv* env,
        jclass,
        jlong handle) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return invalidFrameResult();
        }
        return packedFrameResult(
                controller->drawEnvironmentFieldPreview());
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return invalidFrameResult();
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDrawMicroscope(JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jlong surfaceToken) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return skippedFrameResult();
        }
        auto acquired = lease->tryAcquireFrame(
                static_cast<std::uint64_t>(surfaceToken));
        if (acquired.status != cellularity::app::FrameAcquireStatus::Acquired
                || !acquired.guard.has_value()) {
            return skippedFrameResult();
        }
        ensureGlesResourcesForFrame(
                env, endpointHandle, *lease, *acquired.guard);
        return packedFrameResult(acquired.guard->controller().drawMicroscope());
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return invalidFrameResult();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDiscardSubstratePreviewFrame(
        JNIEnv* env,
        jclass,
        jlong handle,
        jlong token) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller != nullptr && token != 0) {
            controller->discardSubstratePreviewFrame(
                    static_cast<std::uint64_t>(token));
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeClearMicroscopeNoRender(JNIEnv* env,
        jclass,
        jlong endpointHandle,
        jlong surfaceToken) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return skippedFrameResult();
        }
        auto acquired = lease->tryAcquireFrame(
                static_cast<std::uint64_t>(surfaceToken));
        if (acquired.status != cellularity::app::FrameAcquireStatus::Acquired
                || !acquired.guard.has_value()) {
            return skippedFrameResult();
        }
        ensureGlesResourcesForFrame(
                env, endpointHandle, *lease, *acquired.guard);
        return packedFrameResult(
                acquired.guard->controller().clearMicroscopeNoRender());
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return invalidFrameResult();
    }
}
