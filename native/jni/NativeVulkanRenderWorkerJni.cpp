#include <jni.h>

#ifdef __ANDROID__

#include <android/native_window_jni.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include "AndroidRenderAssetsLeaseHandle.h"
#include "NativeJniUtils.h"
#include "NativePreviewEndpointHandle.h"
#include "NativeRenderEndpointHandle.h"
#include "app/render/NativeVulkanDiagnosticsLayout.h"
#include "app/render/NativeVulkanRenderWorker.h"
#include "app/render/NativeVulkanWorkerStatusSignal.h"
#include "app/render/sources/EnvironmentFieldPreviewRenderFrameSource.h"
#include "app/render/sources/GenomePreviewRenderFrameSource.h"
#include "app/render/sources/MicroscopeRenderFrameSource.h"
#include "app/render/sources/VulkanDemoFrameProducer.h"
#include "platform/android/AndroidRenderAssetsLease.h"
#include "platform/android/AndroidVulkanBackendSessionFactory.h"
#include "render/backend/abi/CellularityVulkanBackendAbi.h"

namespace {

using cellularity::app::render_worker::AwaitStoppedResult;
using cellularity::app::render_worker::CompactStatusReadResult;
using cellularity::app::render_worker::DiagnosticsChunkReadStatus;
using cellularity::app::render_worker::DiagnosticsReleaseResult;
using cellularity::app::render_worker::DiagnosticsRequestAdmission;
using cellularity::app::render_worker::FramePacingMode;
using cellularity::app::render_worker::NativeRenderFrameSource;
using cellularity::app::render_worker::NativeVulkanRenderWorker;
using cellularity::app::render_worker::NativeVulkanSurfaceLease;
using cellularity::app::render_worker::NativeVulkanWorkerStatusSignal;
using cellularity::app::render_worker::StatusSignalDelivery;
using cellularity::app::render_worker::WorkerContentKind;
using cellularity::app::render_worker::sources::EnvironmentFieldPreviewRenderFrameSource;
using cellularity::app::render_worker::sources::GenomePreviewRenderFrameSource;
using cellularity::app::render_worker::sources::MicroscopeRenderFrameSource;
using cellularity::app::render_worker::sources::VulkanDemoFrameProducer;
using cellularity::platform::android::AndroidVulkanBackendSessionFactory;

struct DirectBuffer final {
    std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

[[nodiscard]] bool directWritableBuffer(JNIEnv* env, jobject buffer,
        std::size_t minimumSize, DirectBuffer& output) noexcept {
    output = {};
    if (buffer == nullptr)
        return false;
    void* address = env->GetDirectBufferAddress(buffer);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (address == nullptr || capacity < 0
            || static_cast<std::uint64_t>(capacity) < minimumSize
            || static_cast<std::uint64_t>(capacity)
                    > std::numeric_limits<std::size_t>::max()) {
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
    output.data = static_cast<std::uint8_t*>(address);
    output.size = static_cast<std::size_t>(capacity);
    return true;
}

template <typename T>
void writeLittleEndian(std::uint8_t* destination, std::size_t offset,
        T value) noexcept {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        destination[offset + index] = static_cast<std::uint8_t>(
                static_cast<std::uint64_t>(value) >> (index * 8U));
    }
}

[[nodiscard]] const CellularityVulkanSurfaceApiV1* apiFromAddress(
        jlong address) noexcept {
    static_assert(sizeof(std::uintptr_t) <= sizeof(jlong));
    return reinterpret_cast<const CellularityVulkanSurfaceApiV1*>(
            static_cast<std::uintptr_t>(address));
}

[[nodiscard]] std::uint64_t unsignedValue(jlong value) noexcept {
    return static_cast<std::uint64_t>(value);
}

void releaseAndroidWindow(ANativeWindow* window) noexcept {
    if (window != nullptr)
        ANativeWindow_release(window);
}

class JniWorkerStatusSignal final : public NativeVulkanWorkerStatusSignal {
public:
    static std::shared_ptr<JniWorkerStatusSignal> create(
            JNIEnv* env, jobject listener, std::uint64_t generation) {
        if (listener == nullptr || generation == 0
                || generation > static_cast<std::uint64_t>(
                           std::numeric_limits<jlong>::max())) {
            throw std::invalid_argument(
                    "Vulkan worker status listener metadata is invalid");
        }
        JavaVM* vm = nullptr;
        if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
            throw std::logic_error("Could not resolve the Java VM");
        }
        jclass type = env->GetObjectClass(listener);
        if (type == nullptr)
            throw std::bad_alloc();
        const jmethodID method = env->GetMethodID(type,
                "onNativeWorkerStatusSignal", "(JJI)I");
        env->DeleteLocalRef(type);
        if (method == nullptr) {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            throw std::logic_error(
                    "Vulkan worker status listener method is unavailable");
        }
        jobject global = env->NewGlobalRef(listener);
        if (global == nullptr)
            throw std::bad_alloc();
        return std::shared_ptr<JniWorkerStatusSignal>(
                new JniWorkerStatusSignal(vm, global, method, generation));
    }

    ~JniWorkerStatusSignal() override {
        JNIEnv* env = nullptr;
        bool detach = false;
        const jint status = mVm->GetEnv(
                reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (mVm->AttachCurrentThread(&env, nullptr) != JNI_OK
                    || env == nullptr) {
                return;
            }
            detach = true;
        } else if (status != JNI_OK || env == nullptr) {
            return;
        }
        close(env);
        if (detach)
            mVm->DetachCurrentThread();
    }

    StatusSignalDelivery deliver(std::uint64_t statusRevision,
            std::uint32_t actionBits) noexcept override {
        jobject listener = nullptr;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mListener == nullptr)
                return StatusSignalDelivery::ListenerClosed;
            listener = mListener;
        }
        JNIEnv* env = nullptr;
        bool detach = false;
        const jint status = mVm->GetEnv(
                reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (mVm->AttachCurrentThread(&env, nullptr) != JNI_OK
                    || env == nullptr) {
                return StatusSignalDelivery::RetryableFailure;
            }
            detach = true;
        } else if (status != JNI_OK || env == nullptr) {
            return StatusSignalDelivery::RetryableFailure;
        }
        const jint result = env->CallIntMethod(listener, mMethod,
                static_cast<jlong>(mGeneration),
                static_cast<jlong>(statusRevision),
                static_cast<jint>(actionBits));
        StatusSignalDelivery delivery = StatusSignalDelivery::InvalidSignal;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            delivery = StatusSignalDelivery::RetryableFailure;
        } else if (result >= static_cast<jint>(StatusSignalDelivery::Accepted)
                && result <= static_cast<jint>(
                           StatusSignalDelivery::InvalidSignal)) {
            delivery = static_cast<StatusSignalDelivery>(result);
        }
        if (detach)
            mVm->DetachCurrentThread();
        return delivery;
    }

    void close(JNIEnv* env) noexcept {
        jobject listener = nullptr;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            listener = mListener;
            mListener = nullptr;
        }
        if (listener != nullptr)
            env->DeleteGlobalRef(listener);
    }

private:
    JniWorkerStatusSignal(JavaVM* vm, jobject listener,
            jmethodID method, std::uint64_t generation) noexcept
            : mVm(vm),
              mListener(listener),
              mMethod(method),
              mGeneration(generation) {}

    JavaVM* const mVm;
    std::mutex mMutex;
    jobject mListener;
    const jmethodID mMethod;
    const std::uint64_t mGeneration;
};

struct WorkerControlBlock final {
    std::unique_ptr<NativeVulkanRenderWorker> worker;
    std::shared_ptr<JniWorkerStatusSignal> statusSignal;
};

[[nodiscard]] WorkerControlBlock* blockFromHandle(jlong handle) noexcept {
    static_assert(sizeof(std::uintptr_t) <= sizeof(jlong));
    return reinterpret_cast<WorkerControlBlock*>(
            static_cast<std::uintptr_t>(handle));
}

[[nodiscard]] jlong handleFromBlock(WorkerControlBlock* block) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(block));
}

[[nodiscard]] WorkerControlBlock* requireBlock(
        JNIEnv* env, jlong handle) noexcept {
    WorkerControlBlock* block = blockFromHandle(handle);
    if (block == nullptr || block->worker == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Native Vulkan worker handle is unavailable");
        return nullptr;
    }
    return block;
}

[[nodiscard]] bool decodeContentKind(
        jint value, WorkerContentKind& output) noexcept {
    if (value < static_cast<jint>(WorkerContentKind::Microscope)
            || value > static_cast<jint>(WorkerContentKind::Demo)) {
        return false;
    }
    output = static_cast<WorkerContentKind>(value);
    return true;
}

[[nodiscard]] bool decodePacingMode(
        jint value, FramePacingMode& output) noexcept {
    if (value < static_cast<jint>(FramePacingMode::ExternallyPacedContinuous)
            || value > static_cast<jint>(FramePacingMode::OnDemand)) {
        return false;
    }
    output = static_cast<FramePacingMode>(value);
    return true;
}

[[nodiscard]] std::shared_ptr<NativeRenderFrameSource> createFrameSource(
        WorkerContentKind kind, std::uint64_t endpointHandle,
        std::uint64_t endpointGeneration) {
    switch (kind) {
        case WorkerContentKind::Microscope:
            if (endpointHandle != 0 || endpointGeneration != 0) {
                throw std::invalid_argument(
                        "Microscope workers are authorized after creation");
            }
            return nullptr;
        case WorkerContentKind::GenomePreview: {
            if (endpointHandle == 0 || endpointGeneration == 0) {
                throw std::invalid_argument(
                        "Genome Preview worker endpoint is unavailable");
            }
            auto acquired = acquireGenomePreviewEndpoint(
                    endpointHandle, endpointGeneration);
            auto controller = acquirePreviewController(
                    endpointHandle, endpointGeneration);
            if (acquired.status != PreviewEndpointAcquireStatus::Ready
                    || acquired.endpoint == nullptr || controller == nullptr) {
                throw std::logic_error(
                        "Genome Preview worker endpoint is closed or stale");
            }
            return std::make_shared<GenomePreviewRenderFrameSource>(
                    std::move(acquired.endpoint), std::move(controller),
                    endpointGeneration);
        }
        case WorkerContentKind::EnvironmentFieldPreview: {
            if (endpointHandle == 0 || endpointGeneration == 0) {
                throw std::invalid_argument(
                        "Environment preview worker endpoint is unavailable");
            }
            auto acquired = acquireEnvironmentFieldPreviewEndpoint(
                    endpointHandle, endpointGeneration);
            auto controller = acquirePreviewController(
                    endpointHandle, endpointGeneration);
            if (acquired.status != PreviewEndpointAcquireStatus::Ready
                    || acquired.endpoint == nullptr || controller == nullptr) {
                throw std::logic_error(
                        "Environment preview worker endpoint is closed or stale");
            }
            return std::make_shared<EnvironmentFieldPreviewRenderFrameSource>(
                    std::move(acquired.endpoint), std::move(controller),
                    endpointGeneration);
        }
        case WorkerContentKind::Demo:
            if (endpointHandle != 0 || endpointGeneration != 0) {
                throw std::invalid_argument(
                        "Demo workers cannot use a content endpoint");
            }
            return std::make_shared<VulkanDemoFrameProducer>();
    }
    throw std::invalid_argument("Unknown Vulkan worker content kind");
}

void initializeDiagnosticsHeader(std::uint8_t* destination) noexcept {
    using namespace cellularity::app::render_worker;
    std::memset(destination, 0, kNativeVulkanDiagnosticsHeaderSize);
    writeLittleEndian(destination, 0, kNativeVulkanDiagnosticsMagic);
    writeLittleEndian(destination, 4, kNativeVulkanDiagnosticsVersion);
    writeLittleEndian(destination, 6,
            static_cast<std::uint16_t>(kNativeVulkanDiagnosticsHeaderSize));
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeCreateWorkerControlBlock(
        JNIEnv* env, jclass, jlong assetsLeaseHandle, jlong apiTableAddress,
        jlong contentEndpointHandle, jlong contentGeneration,
        jint contentKindValue, jint pacingModeValue, jobject statusListener,
        jlong listenerGeneration) {
    try {
        if (assetsLeaseHandle == 0 || apiTableAddress == 0
                || statusListener == nullptr || listenerGeneration <= 0) {
            throw std::invalid_argument(
                    "Native Vulkan worker creation metadata is invalid");
        }
        WorkerContentKind contentKind{};
        FramePacingMode pacingMode{};
        if (!decodeContentKind(contentKindValue, contentKind)
                || !decodePacingMode(pacingModeValue, pacingMode)) {
            throw std::invalid_argument(
                    "Native Vulkan worker enum value is invalid");
        }
        auto assetsLease = androidRenderAssetsLeaseSharedFromHandle(
                env, assetsLeaseHandle);
        if (assetsLease == nullptr || env->ExceptionCheck())
            return 0;
        auto source = createFrameSource(contentKind,
                unsignedValue(contentEndpointHandle),
                unsignedValue(contentGeneration));
        auto signal = JniWorkerStatusSignal::create(env, statusListener,
                static_cast<std::uint64_t>(listenerGeneration));
        auto factory = std::make_shared<AndroidVulkanBackendSessionFactory>(
                apiFromAddress(apiTableAddress), std::move(assetsLease));
        auto block = std::make_unique<WorkerControlBlock>();
        block->statusSignal = signal;
        block->worker = std::make_unique<NativeVulkanRenderWorker>(
                std::move(factory), std::move(source), contentKind,
                pacingMode, std::move(signal));
        return handleFromBlock(block.release());
    } catch (const std::invalid_argument& failure) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION,
                failure.what());
    } catch (const std::bad_alloc&) {
        throwJavaException(env, "java/lang/OutOfMemoryError",
                "Could not allocate the native Vulkan worker");
    } catch (const std::exception& failure) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                failure.what());
    } catch (...) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Unknown native Vulkan worker creation failure");
    }
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeAuthorizeMicroscopeWorker(
        JNIEnv* env, jclass, jlong workerHandle,
        jlong renderEndpointHandle, jlong surfaceToken) {
    WorkerControlBlock* block = requireBlock(env, workerHandle);
    if (block == nullptr)
        return 6;
    if (renderEndpointHandle == 0 || surfaceToken == 0)
        return 6;
    try {
        auto lease = renderEndpointLeaseFromHandle(
                unsignedValue(renderEndpointHandle));
        const auto result = block->worker->authorizeMicroscope(
                lease, unsignedValue(surfaceToken));
        if (result != cellularity::app::render_worker::WorkerAuthorizationResult::Authorized
                && result != cellularity::app::render_worker::WorkerAuthorizationResult::AlreadyAuthorized) {
            return static_cast<jint>(result);
        }
        const auto binding = block->worker->authorizedMicroscopeBinding();
        if (!binding.has_value()) {
            throw std::logic_error(
                    "Authorized microscope worker has no binding key");
        }
        auto source = std::make_shared<MicroscopeRenderFrameSource>(
                std::move(lease), *binding,
                block->worker->rebindWakeControl());
        if (!block->worker->installDormantFrameSource(std::move(source))) {
            throw std::logic_error(
                    "Could not install the microscope frame source");
        }
        return static_cast<jint>(result);
    } catch (const std::exception& failure) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION, failure.what());
        return 6;
    } catch (...) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Unknown microscope worker authorization failure");
        return 6;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeStartWorker(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 2 : static_cast<jint>(block->worker->start());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeAttachSurface(
        JNIEnv* env, jclass, jlong handle, jobject surface,
        jint width, jint height, jlong generation) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr || surface == nullptr || width < 0 || height < 0
            || generation == 0) {
        return 2;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return 2;
    }
    NativeVulkanSurfaceLease lease(window, &releaseAndroidWindow);
    return static_cast<jint>(block->worker->attachSurface(
            std::move(lease), width, height, unsignedValue(generation)));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeResizeSurface(
        JNIEnv* env, jclass, jlong handle, jint width, jint height,
        jlong generation) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 4
                            : static_cast<jint>(block->worker->resizeSurface(
                                      width, height, unsignedValue(generation)));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeDetachSurface(
        JNIEnv* env, jclass, jlong handle, jlong generation) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 4
                            : static_cast<jint>(block->worker->detachSurface(
                                      unsignedValue(generation)));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeResume(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 4
                            : static_cast<jint>(block->worker->resume());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativePause(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 4
                            : static_cast<jint>(block->worker->pause());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeRequestFrame(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 3
                            : static_cast<jint>(block->worker->requestFrame());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeSetFramePacingMode(
        JNIEnv* env, jclass, jlong handle, jint modeValue) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr)
        return 4;
    FramePacingMode mode{};
    if (!decodePacingMode(modeValue, mode))
        return 2;
    return static_cast<jint>(block->worker->setFramePacingMode(mode));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeSetDemoPerformanceHudEnabled(
        JNIEnv* env, jclass, jlong handle, jboolean enabled) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block == nullptr ? 4
                            : static_cast<jint>(
                                      block->worker->setDemoPerformanceHudEnabled(
                                              enabled == JNI_TRUE));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeReadCompactStatus(
        JNIEnv* env, jclass, jlong handle, jobject destination) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr)
        return static_cast<jint>(
                CompactStatusReadResult::InvalidBuffer);
    DirectBuffer buffer;
    if (!directWritableBuffer(env, destination,
                cellularity::app::render_worker::kNativeVulkanWorkerStatusSize,
                buffer)) {
        return static_cast<jint>(CompactStatusReadResult::InvalidBuffer);
    }
    cellularity::app::render_worker::encodeNativeVulkanWorkerStatus(
            block->worker->status(), buffer.data);
    return static_cast<jint>(CompactStatusReadResult::Ready);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeRequestDiagnostics(
        JNIEnv* env, jclass, jlong handle, jlong requestIdValue) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr)
        return static_cast<jint>(
                DiagnosticsRequestAdmission::Closing);
    const std::uint64_t requestId = unsignedValue(requestIdValue);
    if (requestId == 0 || requestId > static_cast<std::uint64_t>(std::numeric_limits<jlong>::max())) {
        return static_cast<jint>(DiagnosticsRequestAdmission::InvalidRequestId);
    }
    return static_cast<jint>(block->worker->requestDiagnostics(requestId));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeReadDiagnosticsChunk(
        JNIEnv* env, jclass, jlong handle, jlong requestIdValue,
        jlong expectedRevisionValue, jlong offsetValue, jobject destination) {
    using namespace cellularity::app::render_worker;
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr)
        return static_cast<jint>(
                DiagnosticsChunkReadStatus::Failed);
    DirectBuffer buffer;
    if (!directWritableBuffer(env, destination,
                kNativeVulkanDiagnosticsHeaderSize, buffer)) {
        return static_cast<jint>(DiagnosticsChunkReadStatus::InvalidBuffer);
    }
    initializeDiagnosticsHeader(buffer.data);
    const std::uint64_t requestId = unsignedValue(requestIdValue);
    const std::uint64_t expectedRevision =
            unsignedValue(expectedRevisionValue);
    const std::uint64_t offset = unsignedValue(offsetValue);
    const std::size_t payloadCapacity = buffer.size
            - kNativeVulkanDiagnosticsHeaderSize;
    const auto result = block->worker->readDiagnosticsChunk(requestId,
            expectedRevision, offset,
            buffer.data + kNativeVulkanDiagnosticsHeaderSize,
            payloadCapacity);
    writeLittleEndian(buffer.data, 8, result.revision);
    writeLittleEndian(buffer.data, 16, result.totalBytes);
    writeLittleEndian(buffer.data, 24, result.bytesWritten);
    return static_cast<jint>(result.status);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeReleaseDiagnostics(
        JNIEnv* env, jclass, jlong handle, jlong requestIdValue,
        jlong expectedRevisionValue) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr)
        return static_cast<jint>(
                DiagnosticsReleaseResult::AlreadyReleased);
    const std::uint64_t requestId = unsignedValue(requestIdValue);
    const std::uint64_t expectedRevision =
            unsignedValue(expectedRevisionValue);
    return static_cast<jint>(
            block->worker->releaseDiagnostics(requestId, expectedRevision));
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeRequestStop(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block != nullptr)
        block->worker->requestStop();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeAwaitStopped(
        JNIEnv* env, jclass, jlong handle, jlong timeoutMillis) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr || timeoutMillis <= 0) {
        return static_cast<jint>(AwaitStoppedResult::InvalidState);
    }
    return static_cast<jint>(block->worker->awaitStopped(
            std::chrono::milliseconds(timeoutMillis)));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeFinalizeWorkerBinding(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    return block != nullptr && block->worker->finalizeBindingAfterJoin()
            ? JNI_TRUE
            : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanRenderWorkerJni_nativeDestroyStoppedWorker(
        JNIEnv* env, jclass, jlong handle) {
    WorkerControlBlock* block = requireBlock(env, handle);
    if (block == nullptr)
        return;
    if (!block->worker->readyToDestroy()) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Native Vulkan worker is not ready to destroy");
        return;
    }
    block->statusSignal->close(env);
    block->worker.reset();
    block->statusSignal.reset();
    delete block;
}

#endif
