#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#endif

#include "app/render/NativeRenderFrameSource.h"
#include "app/render/NativeRenderProfilingLayout.h"
#include "app/render/NativeRenderProfilingState.h"
#include "app/render/NativeVulkanDiagnosticsLayout.h"
#include "app/render/NativeVulkanRenderWorker.h"
#include "app/render/NativeVulkanSurfaceLease.h"
#include "app/render/NativeVulkanWorkerStatusSignal.h"
#include "render/backend/RenderFrameResult.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanBackendSessionFactory.h"
#include "tests/support/TestAssertions.h"

struct CellularityVulkanSessionOpaque {
    void* context = nullptr;
};

namespace {

using namespace std::chrono_literals;
using cellularity::app::render_worker::AwaitStoppedResult;
using cellularity::app::render_worker::DiagnosticsChunkReadStatus;
using cellularity::app::render_worker::DiagnosticsReleaseResult;
using cellularity::app::render_worker::DiagnosticsRequestAdmission;
using cellularity::app::render_worker::FramePacingMode;
using cellularity::app::render_worker::FrameRequestAdmission;
using cellularity::app::render_worker::NativeRenderFrameSource;
using cellularity::app::render_worker::NativeRenderProfilingDistribution;
using cellularity::app::render_worker::NativeRenderProfilingSnapshot;
using cellularity::app::render_worker::NativeVulkanRenderWorker;
using cellularity::app::render_worker::NativeVulkanSurfaceLease;
using cellularity::app::render_worker::NativeVulkanWorkerFailure;
using cellularity::app::render_worker::NativeVulkanWorkerPhase;
using cellularity::app::render_worker::NativeVulkanWorkerStatusSignal;
using cellularity::app::render_worker::SourceFrameResult;
using cellularity::app::render_worker::SourceFrameStatus;
using cellularity::app::render_worker::SourceSurfaceResult;
using cellularity::app::render_worker::StartWorkerResult;
using cellularity::app::render_worker::StatusSignalDelivery;
using cellularity::app::render_worker::SurfaceDetachReason;
using cellularity::app::render_worker::WorkerCommandAdmission;
using cellularity::app::render_worker::WorkerContentKind;
using cellularity::render::RenderFrameOutcome;
using cellularity::render::abi::VulkanBackendSession;
using cellularity::render::abi::VulkanBackendSessionCreateResult;
using cellularity::render::abi::VulkanBackendSessionFactory;
using cellularity::tests::assertTrue;

struct NiceOperationsProbe final {
    std::array<int, 3> readValues{};
    int readCalls = 0;
    int writeCalls = 0;
    int failReadCall = 0;
    int readFailureError = 0;
    int writeFailureError = 0;
    int writtenValue = 0;
};

bool readNiceProbe(
        void* context, int& value, int& errorNumber) noexcept {
    auto& probe = *static_cast<NiceOperationsProbe*>(context);
    ++probe.readCalls;
    if (probe.readCalls == probe.failReadCall) {
        errorNumber = probe.readFailureError;
        return false;
    }
    value = probe.readValues[static_cast<std::size_t>(probe.readCalls - 1)];
    errorNumber = 0;
    return true;
}

bool writeNiceProbe(
        void* context, int value, int& errorNumber) noexcept {
    auto& probe = *static_cast<NiceOperationsProbe*>(context);
    ++probe.writeCalls;
    probe.writtenValue = value;
    if (probe.writeFailureError != 0) {
        errorNumber = probe.writeFailureError;
        return false;
    }
    errorNumber = 0;
    return true;
}

cellularity::platform::CurrentThreadNiceOperations niceOperations(
        NiceOperationsProbe& probe) noexcept {
    return {&probe, readNiceProbe, writeNiceProbe};
}

struct SchedulingBootstrapProbe final {
    std::atomic<int> sequence{0};
    std::atomic<int> normalizationCalls{0};
    std::atomic<int> factoryCalls{0};
    std::atomic<int> normalizationSequence{0};
    std::atomic<int> factorySequence{0};
    std::atomic<bool> factorySawNormalization{false};
};

std::atomic<SchedulingBootstrapProbe*> gSchedulingBootstrapProbe{nullptr};

cellularity::platform::CurrentThreadSchedulingNormalizationResult
probeSchedulingNormalizer() noexcept {
    auto* probe = gSchedulingBootstrapProbe.load(std::memory_order_acquire);
    cellularity::platform::CurrentThreadSchedulingNormalizationResult result;
    result.status = cellularity::platform::
            CurrentThreadSchedulingNormalizationStatus::Applied;
    result.inheritedNice = -10;
    result.effectiveNice = 0;
    result.inheritedNiceKnown = true;
    result.effectiveNiceKnown = true;
    if (probe != nullptr) {
        ++probe->normalizationCalls;
        probe->normalizationSequence = ++probe->sequence;
    }
    return result;
}

template <std::size_t Size, typename T>
void writeGoldenLittleEndian(std::array<std::uint8_t, Size>& destination,
        std::size_t offset, T value) {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        destination[offset + index] = static_cast<std::uint8_t>(
                static_cast<std::uint64_t>(value) >> (index * 8U));
    }
}

template <std::size_t Size>
void writeGoldenDistribution(std::array<std::uint8_t, Size>& destination,
        std::size_t offset,
        const NativeRenderProfilingDistribution& distribution) {
    writeGoldenLittleEndian(destination, offset, distribution.sampleCount);
    writeGoldenLittleEndian(destination, offset + 8, distribution.p50Nanos);
    writeGoldenLittleEndian(destination, offset + 16, distribution.p95Nanos);
    writeGoldenLittleEndian(destination, offset + 24, distribution.p99Nanos);
    writeGoldenLittleEndian(destination, offset + 32,
            distribution.maximumNanos);
}

struct FakeBackend final {
    CellularityVulkanSessionOpaque session{this};
    std::atomic<int> createCount{0};
    std::atomic<int> destroyCount{0};
    std::atomic<int> retainCount{0};
    std::atomic<int> releaseCount{0};
    std::atomic<int> attachCount{0};
    std::atomic<int> resizeCount{0};
    std::atomic<int> detachCount{0};
    std::atomic<int> consumedWindows{0};
    std::atomic<int> profilingCalls{0};
    std::atomic<int> diagnosticsSizeCount{0};
    std::atomic<int> diagnosticsChunkCount{0};
    std::atomic<int> diagnosticsReleaseCount{0};
    std::atomic<bool> diagnosticsAffinityValid{true};
    std::atomic<std::uint32_t> operationStatus{
            CELLULARITY_VULKAN_OPERATION_OK_V1
    };
    std::thread::id createThread;
    std::thread::id destroyThread;
    std::thread::id lastOperationThread;
    std::mutex attachMutex;
    std::condition_variable attachWake;
    bool blockAttach = false;
    bool attachEntered = false;
    bool allowAttach = false;
    std::mutex resizeMutex;
    std::condition_variable resizeWake;
    bool blockResize = false;
    bool resizeEntered = false;
    bool allowResize = false;
    std::mutex diagnosticsMutex;
    std::condition_variable diagnosticsWake;
    std::string diagnosticsText{"fake_backend_diagnostics=ready\n"};
    bool diagnosticsActive = false;
    bool blockDiagnostics = false;
    bool diagnosticsEntered = false;
    bool allowDiagnostics = false;

    void blockNextAttach() {
        std::lock_guard<std::mutex> lock(attachMutex);
        blockAttach = true;
        attachEntered = false;
        allowAttach = false;
    }

    bool waitForAttach(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(attachMutex);
        return attachWake.wait_for(lock, timeout, [&] {
            return attachEntered;
        });
    }

    void releaseAttach() {
        std::lock_guard<std::mutex> lock(attachMutex);
        allowAttach = true;
        attachWake.notify_all();
    }

    void blockNextResize() {
        std::lock_guard<std::mutex> lock(resizeMutex);
        blockResize = true;
        resizeEntered = false;
        allowResize = false;
    }

    bool waitForResize(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(resizeMutex);
        return resizeWake.wait_for(lock, timeout, [&] {
            return resizeEntered;
        });
    }

    void releaseResize() {
        std::lock_guard<std::mutex> lock(resizeMutex);
        allowResize = true;
        resizeWake.notify_all();
    }

    void blockNextDiagnostics() {
        std::lock_guard<std::mutex> lock(diagnosticsMutex);
        blockDiagnostics = true;
        diagnosticsEntered = false;
        allowDiagnostics = false;
    }

    bool waitForDiagnostics(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(diagnosticsMutex);
        return diagnosticsWake.wait_for(lock, timeout, [&] {
            return diagnosticsEntered;
        });
    }

    void releaseBlockedDiagnostics() {
        std::lock_guard<std::mutex> lock(diagnosticsMutex);
        allowDiagnostics = true;
        diagnosticsWake.notify_all();
    }
};

std::atomic<FakeBackend*> gCreatingBackend{nullptr};

FakeBackend& backend(CellularityVulkanSessionHandle session) {
    return *static_cast<FakeBackend*>(session->context);
}

void initializeSuccess(CellularityVulkanErrorV1* error) {
    std::memset(error, 0, sizeof(*error));
    error->abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    error->struct_size = sizeof(*error);
}

void retainApi(void* context) {
    ++static_cast<FakeBackend*>(context)->retainCount;
}

void releaseApi(void* context) {
    ++static_cast<FakeBackend*>(context)->releaseCount;
}

CellularityVulkanSessionHandle createSession(
        const CellularityVulkanSessionCreateInfoV1*,
        CellularityVulkanErrorV1* error) {
    FakeBackend* fake = gCreatingBackend.load(std::memory_order_acquire);
    initializeSuccess(error);
    ++fake->createCount;
    fake->createThread = std::this_thread::get_id();
    return &fake->session;
}

void destroySession(CellularityVulkanSessionHandle session) {
    auto& fake = backend(session);
    ++fake.destroyCount;
    fake.destroyThread = std::this_thread::get_id();
}

CellularityVulkanOperationStatusV1 hasAttachedSurface(
        CellularityVulkanSessionHandle session, std::uint32_t* attached) {
    backend(session).lastOperationThread = std::this_thread::get_id();
    *attached = 0;
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanOperationStatusV1 attachWindow(
        CellularityVulkanSessionHandle session, ANativeWindow*,
        std::int32_t, std::int32_t) {
    auto& fake = backend(session);
    fake.lastOperationThread = std::this_thread::get_id();
    {
        std::unique_lock<std::mutex> lock(fake.attachMutex);
        if (fake.blockAttach) {
            fake.attachEntered = true;
            fake.attachWake.notify_all();
            fake.attachWake.wait(lock, [&] {
                return fake.allowAttach;
            });
            fake.blockAttach = false;
        }
    }
    ++fake.attachCount;
    ++fake.consumedWindows;
    return fake.operationStatus.load();
}

CellularityVulkanOperationStatusV1 resizeSurface(
        CellularityVulkanSessionHandle session, std::int32_t, std::int32_t) {
    auto& fake = backend(session);
    fake.lastOperationThread = std::this_thread::get_id();
    {
        std::unique_lock<std::mutex> lock(fake.resizeMutex);
        if (fake.blockResize) {
            fake.resizeEntered = true;
            fake.resizeWake.notify_all();
            fake.resizeWake.wait(lock, [&] {
                return fake.allowResize;
            });
            fake.blockResize = false;
        }
    }
    ++fake.resizeCount;
    return fake.operationStatus.load();
}

CellularityVulkanOperationStatusV1 detachSurface(
        CellularityVulkanSessionHandle session) {
    auto& fake = backend(session);
    fake.lastOperationThread = std::this_thread::get_id();
    ++fake.detachCount;
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanSurfaceOutcomeV1 presentMicroscope(
        CellularityVulkanSessionHandle,
        const CellularityVulkanPresentationStateV1*,
        const CellularityPreparedRenderFrameViewV1*,
        const CellularityVulkanDebugHudViewV1*) {
    return CELLULARITY_VULKAN_SURFACE_PRESENTED_V1;
}

CellularityVulkanSurfaceOutcomeV1 clearSurface(
        CellularityVulkanSessionHandle,
        const CellularityVulkanViewportStateV1*, const float[4]) {
    return CELLULARITY_VULKAN_SURFACE_PRESENTED_V1;
}

CellularityVulkanSurfaceOutcomeV1 drawEnvironmentFieldPreview(
        CellularityVulkanSessionHandle,
        const CellularityVulkanEnvironmentFieldPreviewCommandV1*) {
    return CELLULARITY_VULKAN_SURFACE_PRESENTED_V1;
}

CellularityVulkanOperationStatusV1 cancelGenomeFrame(
        CellularityVulkanSessionHandle) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanOperationStatusV1 beginGenomeFrame(
        CellularityVulkanSessionHandle, std::int32_t, std::int32_t,
        std::uint32_t) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanOperationStatusV1 drawGenomeGrid(
        CellularityVulkanSessionHandle,
        const CellularityVulkanGenomeGridCommandV1*) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanOperationStatusV1 drawGenomeTree(
        CellularityVulkanSessionHandle,
        const CellularityVulkanPresentationStateV1*,
        const CellularityPreparedRenderFrameViewV1*, const float[16],
        CellularityVulkanFloatSpanV1,
        const CellularityVulkanGenomeDrawOptionsV1*) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanOperationStatusV1 drawGenomeCells(
        CellularityVulkanSessionHandle,
        const CellularityVulkanPresentationStateV1*,
        const CellularityPreparedRenderFrameViewV1*,
        const CellularityVulkanGenomeDrawOptionsV1*) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanSurfaceOutcomeV1 endGenomeSurfaceFrame(
        CellularityVulkanSessionHandle) {
    return CELLULARITY_VULKAN_SURFACE_PRESENTED_V1;
}

CellularityVulkanOperationStatusV1 beginOffscreen(
        CellularityVulkanSessionHandle, CellularityVulkanOffscreenTargetV1,
        std::int32_t, std::int32_t) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanPassStatusV1 passCompleted(
        CellularityVulkanSessionHandle,
        const CellularityVulkanPresentationStateV1*,
        const CellularityPreparedRenderFrameViewV1*) {
    return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
}

CellularityVulkanPassStatusV1 endOffscreen(
        CellularityVulkanSessionHandle) {
    return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
}

CellularityVulkanPassStatusV1 finishOffscreen(
        CellularityVulkanSessionHandle, CellularityVulkanOffscreenTargetV1,
        std::uint8_t*, std::uint64_t, std::uint64_t* written) {
    *written = 0;
    return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
}

CellularityVulkanOperationStatusV1 cancelOffscreen(
        CellularityVulkanSessionHandle, CellularityVulkanOffscreenTargetV1) {
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanOperationStatusV1 setProfiling(
        CellularityVulkanSessionHandle session, std::uint32_t) {
    ++backend(session).profilingCalls;
    return CELLULARITY_VULKAN_OPERATION_OK_V1;
}

CellularityVulkanDiagnosticsStatusV1 diagnosticsSize(
        CellularityVulkanSessionHandle session, std::uint64_t* revision,
        std::uint64_t* bytes) {
    auto& fake = backend(session);
    if (fake.createThread != std::this_thread::get_id())
        fake.diagnosticsAffinityValid = false;
    ++fake.diagnosticsSizeCount;
    std::unique_lock<std::mutex> lock(fake.diagnosticsMutex);
    if (fake.blockDiagnostics) {
        fake.diagnosticsEntered = true;
        fake.diagnosticsWake.notify_all();
        fake.diagnosticsWake.wait(lock, [&] {
            return fake.allowDiagnostics;
        });
        fake.blockDiagnostics = false;
    }
    if (fake.diagnosticsActive)
        return CELLULARITY_VULKAN_DIAGNOSTICS_BUSY_V1;
    fake.diagnosticsActive = true;
    *revision = static_cast<std::uint64_t>(fake.diagnosticsSizeCount.load());
    *bytes = fake.diagnosticsText.size();
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanDiagnosticsStatusV1 diagnosticsChunk(
        CellularityVulkanSessionHandle session, std::uint64_t,
        std::uint64_t offset, std::uint8_t* destination,
        std::uint32_t capacity, std::uint32_t* written) {
    auto& fake = backend(session);
    if (fake.createThread != std::this_thread::get_id())
        fake.diagnosticsAffinityValid = false;
    ++fake.diagnosticsChunkCount;
    std::lock_guard<std::mutex> lock(fake.diagnosticsMutex);
    if (!fake.diagnosticsActive || offset > fake.diagnosticsText.size())
        return CELLULARITY_VULKAN_DIAGNOSTICS_STALE_REVISION_V1;
    const std::size_t count = std::min<std::size_t>(capacity,
            fake.diagnosticsText.size() - static_cast<std::size_t>(offset));
    if (count != 0) {
        std::memcpy(destination,
                fake.diagnosticsText.data() + static_cast<std::size_t>(offset),
                count);
    }
    *written = static_cast<std::uint32_t>(count);
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanDiagnosticsStatusV1 releaseDiagnostics(
        CellularityVulkanSessionHandle session, std::uint64_t) {
    auto& fake = backend(session);
    if (fake.createThread != std::this_thread::get_id())
        fake.diagnosticsAffinityValid = false;
    ++fake.diagnosticsReleaseCount;
    std::lock_guard<std::mutex> lock(fake.diagnosticsMutex);
    if (!fake.diagnosticsActive)
        return CELLULARITY_VULKAN_DIAGNOSTICS_STALE_REVISION_V1;
    fake.diagnosticsActive = false;
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanSurfaceApiV1 makeApi(FakeBackend& fake) {
    return {
            CELLULARITY_VULKAN_ABI_VERSION_V1,
            sizeof(CellularityVulkanSurfaceApiV1),
            &fake,
            retainApi,
            releaseApi,
            createSession,
            destroySession,
            hasAttachedSurface,
            attachWindow,
            resizeSurface,
            detachSurface,
            presentMicroscope,
            clearSurface,
            drawEnvironmentFieldPreview,
            beginGenomeFrame,
            drawGenomeGrid,
            drawGenomeTree,
            drawGenomeCells,
            endGenomeSurfaceFrame,
            cancelGenomeFrame,
            beginOffscreen,
            passCompleted,
            endOffscreen,
            finishOffscreen,
            cancelOffscreen,
            setProfiling,
            diagnosticsSize,
            diagnosticsChunk,
            releaseDiagnostics,
    };
}

class FakeFactory final : public VulkanBackendSessionFactory {
public:
    explicit FakeFactory(FakeBackend& backend,
            SchedulingBootstrapProbe* schedulingProbe = nullptr,
            bool failCreation = false)
            : mBackend(backend),
              mApi(makeApi(backend)),
              mSchedulingProbe(schedulingProbe),
              mFailCreation(failCreation) {}

    VulkanBackendSessionCreateResult createOnOwnerThread() noexcept override {
        VulkanBackendSessionCreateResult result;
        initializeSuccess(&result.error);
        if (mSchedulingProbe != nullptr) {
            ++mSchedulingProbe->factoryCalls;
            mSchedulingProbe->factorySawNormalization =
                    mSchedulingProbe->normalizationCalls.load(
                            std::memory_order_acquire)
                    == 1;
            mSchedulingProbe->factorySequence = ++mSchedulingProbe->sequence;
        }
        if (mFailCreation) {
            result.error.code =
                    CELLULARITY_VULKAN_ERROR_BACKEND_INITIALIZATION_FAILED_V1;
            return result;
        }
        try {
            gCreatingBackend.store(&mBackend, std::memory_order_release);
            result.session = VulkanBackendSession::create(
                    &mApi,
                    reinterpret_cast<AAssetManager*>(
                            static_cast<std::uintptr_t>(0x1000)));
        } catch (...) {
            result.error.code =
                    CELLULARITY_VULKAN_ERROR_BACKEND_INITIALIZATION_FAILED_V1;
        }
        return result;
    }

private:
    FakeBackend& mBackend;
    CellularityVulkanSurfaceApiV1 mApi;
    SchedulingBootstrapProbe* mSchedulingProbe;
    bool mFailCreation;
};

class FakeSource final : public NativeRenderFrameSource {
public:
    void setPerformanceHudEnabled(bool enabled) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        hudStates.push_back(enabled);
    }

    SourceSurfaceResult bindBackend(
            std::shared_ptr<VulkanBackendSession> session) noexcept override {
        ownerThread = std::this_thread::get_id();
        backend = std::move(session);
        ++bindCount;
        return bindResult.load(std::memory_order_acquire);
    }

    SourceSurfaceResult unbindBackend() noexcept override {
        assertOwnerThread();
        ++unbindCount;
        const SourceSurfaceResult result =
                unbindResult.load(std::memory_order_acquire);
        if (result == SourceSurfaceResult::Applied) {
            backend.reset();
        }
        return result;
    }

    SourceSurfaceResult onSurfaceAttached(
            std::uint64_t generation, int width, int height) noexcept override {
        assertOwnerThread();
        ++attachCount;
        lastGeneration = generation;
        lastWidth = width;
        lastHeight = height;
        return attachResult.load(std::memory_order_acquire);
    }

    SourceSurfaceResult onSurfaceResized(
            int width, int height) noexcept override {
        assertOwnerThread();
        ++resizeCount;
        lastWidth = width;
        lastHeight = height;
        return resizeResult.load(std::memory_order_acquire);
    }

    SourceSurfaceResult onSurfaceDetached(
            SurfaceDetachReason reason) noexcept override {
        assertOwnerThread();
        ++detachCount;
        lastDetachReason = reason;
        return detachResult.load(std::memory_order_acquire);
    }

    SourceFrameResult renderFrame() noexcept override {
        assertOwnerThread();
        ++frameCount;
        std::unique_lock<std::mutex> lock(mutex);
        if (blockFrame) {
            frameEntered = true;
            frameWake.notify_all();
            frameWake.wait(lock, [&] {
                return allowFrame;
            });
            blockFrame = false;
        }
        const SourceFrameStatus frameStatus =
                sourceFrameStatus.load(std::memory_order_acquire);
        if (frameStatus != SourceFrameStatus::Completed) {
            return {frameStatus, {}};
        }
        if (frames.empty()) {
            return {SourceFrameStatus::Completed,
                    cellularity::render::makeRenderFrameResult(
                            RenderFrameOutcome::Presented,
                            cellularity::render::kPrimaryContentPresented)};
        }
        const auto frame = frames.front();
        frames.pop_front();
        return {SourceFrameStatus::Completed, frame};
    }

    void enqueue(RenderFrameOutcome outcome) {
        std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(cellularity::render::makeRenderFrameResult(outcome,
                outcome == RenderFrameOutcome::Presented
                        ? cellularity::render::kPrimaryContentPresented
                        : 0));
    }

    void enqueueFrame(cellularity::render::RenderFrameResult frame) {
        std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(frame);
    }

    void blockNextFrame() {
        std::lock_guard<std::mutex> lock(mutex);
        blockFrame = true;
        frameEntered = false;
        allowFrame = false;
    }

    bool waitForFrame(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return frameWake.wait_for(lock, timeout, [&] {
            return frameEntered;
        });
    }

    void releaseFrame() {
        std::lock_guard<std::mutex> lock(mutex);
        allowFrame = true;
        frameWake.notify_all();
    }

    void assertOwnerThread() noexcept {
        if (ownerThread != std::this_thread::get_id())
            affinityValid = false;
    }

    std::shared_ptr<VulkanBackendSession> backend;
    std::mutex mutex;
    std::condition_variable frameWake;
    std::deque<cellularity::render::RenderFrameResult> frames;
    std::vector<bool> hudStates;
    std::thread::id ownerThread;
    std::atomic<int> bindCount{0};
    std::atomic<int> unbindCount{0};
    std::atomic<int> attachCount{0};
    std::atomic<int> resizeCount{0};
    std::atomic<int> detachCount{0};
    std::atomic<int> frameCount{0};
    std::atomic<bool> affinityValid{true};
    std::atomic<SourceSurfaceResult> bindResult{SourceSurfaceResult::Applied};
    std::atomic<SourceSurfaceResult> unbindResult{
            SourceSurfaceResult::Applied
    };
    std::atomic<SourceSurfaceResult> attachResult{
            SourceSurfaceResult::Applied
    };
    std::atomic<SourceSurfaceResult> resizeResult{
            SourceSurfaceResult::Applied
    };
    std::atomic<SourceSurfaceResult> detachResult{
            SourceSurfaceResult::Applied
    };
    std::atomic<SourceFrameStatus> sourceFrameStatus{
            SourceFrameStatus::Completed
    };
    std::atomic<SurfaceDetachReason> lastDetachReason{
            SurfaceDetachReason::Ordinary
    };
    std::atomic<std::uint64_t> lastGeneration{0};
    std::atomic<int> lastWidth{0};
    std::atomic<int> lastHeight{0};
    bool blockFrame = false;
    bool frameEntered = false;
    bool allowFrame = false;
};

class FakeSignal final : public NativeVulkanWorkerStatusSignal {
public:
    StatusSignalDelivery deliver(
            std::uint64_t revision, std::uint32_t actions) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        revisions.push_back(revision);
        actionBits.push_back(actions);
        deliveryTimes.push_back(std::chrono::steady_clock::now());
        if (!results.empty()) {
            const auto result = results.front();
            results.pop_front();
            return result;
        }
        return StatusSignalDelivery::Accepted;
    }

    std::mutex mutex;
    std::deque<StatusSignalDelivery> results;
    std::vector<std::uint64_t> revisions;
    std::vector<std::uint32_t> actionBits;
    std::vector<std::chrono::steady_clock::time_point> deliveryTimes;
};

std::atomic<int> gReleasedWindows{0};

void releaseWindow(ANativeWindow*) noexcept {
    ++gReleasedWindows;
}

NativeVulkanSurfaceLease surface(std::uintptr_t identity) {
    return NativeVulkanSurfaceLease(
            reinterpret_cast<ANativeWindow*>(identity), releaseWindow);
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate, std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::yield();
    }
    return predicate();
}

std::string waitForDiagnosticsReport(NativeVulkanRenderWorker& worker,
        std::uint64_t requestId, std::uint64_t expectedRevision,
        std::uint64_t& observedRevision) {
    std::array<std::uint8_t, 97> chunk{};
    std::string report;
    std::uint64_t totalBytes = 0;
    bool totalKnown = false;
    const bool ready = waitUntil([&] {
        const auto result = worker.readDiagnosticsChunk(requestId,
                expectedRevision == 0 ? observedRevision : expectedRevision,
                report.size(), chunk.data(), chunk.size());
        if (result.status == DiagnosticsChunkReadStatus::Pending)
            return false;
        assertTrue(result.status == DiagnosticsChunkReadStatus::Ready,
                "Diagnostics request must become readable");
        if (observedRevision == 0)
            observedRevision = result.revision;
        assertTrue(result.revision == observedRevision,
                "Diagnostics revision must remain immutable across chunks");
        if (!totalKnown) {
            totalBytes = result.totalBytes;
            totalKnown = true;
        }
        assertTrue(result.totalBytes == totalBytes,
                "Diagnostics size must remain immutable across chunks");
        assertTrue(result.bytesWritten <= chunk.size(),
                "Diagnostics chunk must fit the caller buffer");
        report.append(reinterpret_cast<const char*>(chunk.data()),
                result.bytesWritten);
        assertTrue(report.size() <= totalBytes,
                "Diagnostics reader must not pass the advertised size");
        return report.size() == totalBytes;
    });
    assertTrue(ready, "Diagnostics request must complete before timeout");
    return report;
}

void stopWorker(NativeVulkanRenderWorker& worker) {
    worker.requestStop();
    assertTrue(worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Worker must stop within the bounded test timeout");
    assertTrue(worker.finalizeBindingAfterJoin(),
            "Joined worker binding must finalize");
    assertTrue(worker.readyToDestroy(),
            "Finalized worker must be ready for destruction");
}

struct WorkerFixture final {
    FakeBackend backend;
    std::shared_ptr<FakeSource> source = std::make_shared<FakeSource>();
    std::shared_ptr<FakeSignal> signal = std::make_shared<FakeSignal>();
    NativeVulkanRenderWorker worker;

    explicit WorkerFixture(
            FramePacingMode pacing = FramePacingMode::OnDemand)
            : worker(std::make_shared<FakeFactory>(backend), source,
                      WorkerContentKind::Demo, pacing, signal) {}
};

void testInheritedNiceNormalizationPolicyAndFailures() {
    using Status = cellularity::platform::
            CurrentThreadSchedulingNormalizationStatus;

    for (int inheritedNice = -20; inheritedNice < 0; ++inheritedNice) {
        NiceOperationsProbe probe;
        probe.readValues = {inheritedNice, 0, 0};
        const auto result = cellularity::platform::
                normalizeInheritedThreadPriorityBoost(niceOperations(probe));
        assertTrue(result.status == Status::Applied
                        && result.inheritedNiceKnown
                        && result.inheritedNice == inheritedNice
                        && result.effectiveNiceKnown
                        && result.effectiveNice == 0,
                "Every inherited priority boost must normalize to nice zero");
        assertTrue(probe.writeCalls == 1 && probe.writtenValue == 0
                        && probe.readCalls == 2,
                "Boost normalization must write and verify exactly once");
    }

    for (int inheritedNice = 0; inheritedNice <= 19; ++inheritedNice) {
        NiceOperationsProbe probe;
        probe.readValues = {inheritedNice, 0, 0};
        const auto result = cellularity::platform::
                normalizeInheritedThreadPriorityBoost(niceOperations(probe));
        assertTrue(result.status == Status::NotNeeded
                        && result.inheritedNice == inheritedNice
                        && result.effectiveNice == inheritedNice,
                "Neutral and intentionally restricted nice values must remain unchanged");
        assertTrue(probe.writeCalls == 0 && probe.readCalls == 1,
                "A non-negative nice value must not trigger a write");
    }

    {
        NiceOperationsProbe probe;
        probe.failReadCall = 1;
        probe.readFailureError = EIO;
        const auto result = cellularity::platform::
                normalizeInheritedThreadPriorityBoost(niceOperations(probe));
        assertTrue(result.status == Status::ReadFailed
                        && result.errorNumber == EIO
                        && !result.inheritedNiceKnown
                        && probe.writeCalls == 0,
                "Initial read failure must remain distinguishable");
    }
    {
        NiceOperationsProbe probe;
        probe.readValues = {-7, -7, 0};
        probe.writeFailureError = EACCES;
        const auto result = cellularity::platform::
                normalizeInheritedThreadPriorityBoost(niceOperations(probe));
        assertTrue(result.status == Status::WriteFailed
                        && result.errorNumber == EACCES
                        && result.inheritedNice == -7
                        && result.effectiveNiceKnown
                        && result.effectiveNice == -7,
                "Write failure must retain its own status and observed value");
    }
    {
        NiceOperationsProbe probe;
        probe.readValues = {-3, 0, 0};
        probe.failReadCall = 2;
        probe.readFailureError = EIO;
        const auto result = cellularity::platform::
                normalizeInheritedThreadPriorityBoost(niceOperations(probe));
        assertTrue(result.status == Status::VerificationFailed
                        && result.errorNumber == EIO
                        && !result.effectiveNiceKnown,
                "Post-write read failure must be a verification failure");
    }
    {
        NiceOperationsProbe probe;
        probe.readValues = {-2, -1, 0};
        const auto result = cellularity::platform::
                normalizeInheritedThreadPriorityBoost(niceOperations(probe));
        assertTrue(result.status == Status::VerificationFailed
                        && result.errorNumber == 0
                        && result.effectiveNiceKnown
                        && result.effectiveNice == -1,
                "A mismatched effective nice value must fail verification");
    }
}

void testHostSchedulingAdapterDoesNotChangeRunnerNice() {
#if !defined(__ANDROID__)
#if defined(__linux__)
    errno = 0;
    const int before = getpriority(PRIO_PROCESS, 0);
    const int beforeError = errno;
#endif
    const auto result = cellularity::platform::
            normalizeCurrentThreadInheritedPriorityBoost();
    assertTrue(result.status == cellularity::platform::CurrentThreadSchedulingNormalizationStatus::NotApplicable,
            "Host scheduling adapter must be an explicit no-op");
#if defined(__linux__)
    errno = 0;
    const int after = getpriority(PRIO_PROCESS, 0);
    const int afterError = errno;
    assertTrue(beforeError == afterError && before == after,
            "Host scheduling adapter must not change the test runner nice value");
#endif
#endif
}

void testSchedulingNormalizationPrecedesSessionCreation() {
    SchedulingBootstrapProbe probe;
    gSchedulingBootstrapProbe.store(&probe, std::memory_order_release);
    FakeBackend backend;
    auto source = std::make_shared<FakeSource>();
    auto signal = std::make_shared<FakeSignal>();
    NativeVulkanRenderWorker worker(
            std::make_shared<FakeFactory>(backend, &probe), source,
            WorkerContentKind::Demo, FramePacingMode::OnDemand, signal,
            probeSchedulingNormalizer);
    assertTrue(worker.diagnosticsReport().find(
                       "normalization_status=not_run")
                    != std::string::npos,
            "Dormant diagnostics must distinguish a normalizer that has not run");
    assertTrue(worker.start() == StartWorkerResult::Started,
            "Worker must start for scheduling bootstrap verification");
    assertTrue(waitUntil([&] {
        return source->bindCount.load(std::memory_order_acquire) == 1;
    }),
            "Worker must bind after scheduling normalization");
    assertTrue(probe.normalizationCalls.load() == 1
                    && probe.factoryCalls.load() == 1
                    && probe.factorySawNormalization.load()
                    && probe.normalizationSequence.load()
                            < probe.factorySequence.load(),
            "Scheduling normalization must run exactly once before session creation");

    assertTrue(worker.requestDiagnostics(1)
                    == DiagnosticsRequestAdmission::Accepted,
            "Scheduling diagnostics request must be accepted");
    std::uint64_t revision = 0;
    const std::string report =
            waitForDiagnosticsReport(worker, 1, 0, revision);
    assertTrue(report.find("normalization_status=applied")
                            != std::string::npos
                    && report.find("inherited_nice=-10")
                            != std::string::npos
                    && report.find("effective_nice=0")
                            != std::string::npos,
            "Full diagnostics must expose scheduling before, after and status");
    assertTrue(worker.releaseDiagnostics(1, revision)
                    == DiagnosticsReleaseResult::Released,
            "Scheduling diagnostics must release normally");
    stopWorker(worker);
    assertTrue(probe.normalizationCalls.load() == 1,
            "Shutdown must not repeat scheduling normalization");
    gSchedulingBootstrapProbe.store(nullptr, std::memory_order_release);
}

void testFailedSessionStartupDoesNotRepeatSchedulingNormalization() {
    SchedulingBootstrapProbe probe;
    gSchedulingBootstrapProbe.store(&probe, std::memory_order_release);
    FakeBackend backend;
    auto source = std::make_shared<FakeSource>();
    auto signal = std::make_shared<FakeSignal>();
    NativeVulkanRenderWorker worker(
            std::make_shared<FakeFactory>(backend, &probe, true), source,
            WorkerContentKind::Demo, FramePacingMode::OnDemand, signal,
            probeSchedulingNormalizer);
    assertTrue(worker.start() == StartWorkerResult::Started,
            "Worker thread must start before simulated session failure");
    assertTrue(waitUntil([&] {
        return worker.status().phase
                == NativeVulkanWorkerPhase::TerminalFailure;
    }),
            "Simulated session creation failure must become terminal");
    assertTrue(worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Failed session startup must terminate the worker");
    assertTrue(worker.finalizeBindingAfterJoin(),
            "Failed startup worker must still finalize");
    assertTrue(probe.normalizationCalls.load() == 1
                    && probe.factoryCalls.load() == 1
                    && probe.factorySawNormalization.load(),
            "Terminal startup failure must not repeat normalization");
    const auto diagnostics = worker.failureDiagnosticsSnapshot();
    assertTrue(diagnostics.report.find("normalization_status=applied")
                            != std::string::npos
                    && diagnostics.report.find("effective_nice=0")
                            != std::string::npos,
            "Startup failure diagnostics must retain scheduling outcome");
    gSchedulingBootstrapProbe.store(nullptr, std::memory_order_release);
}

void testSurfaceLeaseOwnership() {
    gReleasedWindows = 0;
    {
        auto first = surface(1);
        auto second = std::move(first);
        assertTrue(!first && second,
                "Moving a native window lease must transfer ownership");
        second.reset();
    }
    assertTrue(gReleasedWindows == 1,
            "A pending window lease must release exactly once");
    {
        auto consumed = surface(2);
        assertTrue(consumed.takeForBackend()
                        == reinterpret_cast<ANativeWindow*>(2),
                "Backend handoff must return the acquired window");
    }
    assertTrue(gReleasedWindows == 1,
            "A consumed backend window must not be released by the lease");
}

void testWorkerStatusWireLayoutMatchesGoldenBytes() {
    cellularity::app::render_worker::NativeVulkanWorkerStatus status;
    status.revision = UINT64_C(0x0102030405060708);
    status.phase = NativeVulkanWorkerPhase::RecoverableFailure;
    status.lastFrameOutcome = 5;
    status.flags = cellularity::app::render_worker::kWorkerDesiredResumed
            | cellularity::app::render_worker::kWorkerHasFrameOutcome;
    status.failure = NativeVulkanWorkerFailure::FrameFailed;
    status.presentedFrames = 11;
    status.swapchainRecreations = 12;
    status.failedFrameAttempts = 13;
    status.desiredSurfaceGeneration = 14;
    status.appliedSurfaceGeneration = 15;
    status.diagnosticsRevision = 16;
    status.desiredWidth = 1920;
    status.desiredHeight = 1080;

    std::array<std::uint8_t,
            cellularity::app::render_worker::
                    kNativeVulkanWorkerStatusSize>
            encoded{};
    cellularity::app::render_worker::encodeNativeVulkanWorkerStatus(
            status, encoded.data());
    decltype(encoded) expected{};
    writeGoldenLittleEndian(expected, 0,
            cellularity::app::render_worker::
                    kNativeVulkanWorkerStatusMagic);
    writeGoldenLittleEndian(expected, 4,
            cellularity::app::render_worker::
                    kNativeVulkanWorkerStatusVersion);
    writeGoldenLittleEndian(expected, 6,
            static_cast<std::uint16_t>(expected.size()));
    writeGoldenLittleEndian(expected, 8, status.revision);
    writeGoldenLittleEndian(expected, 16,
            static_cast<std::uint32_t>(status.phase));
    writeGoldenLittleEndian(expected, 20, status.lastFrameOutcome);
    writeGoldenLittleEndian(expected, 24, status.flags);
    writeGoldenLittleEndian(expected, 28,
            static_cast<std::uint32_t>(status.failure));
    writeGoldenLittleEndian(expected, 32, status.presentedFrames);
    writeGoldenLittleEndian(expected, 40, status.swapchainRecreations);
    writeGoldenLittleEndian(expected, 48, status.failedFrameAttempts);
    writeGoldenLittleEndian(expected, 56, status.desiredSurfaceGeneration);
    writeGoldenLittleEndian(expected, 64, status.appliedSurfaceGeneration);
    writeGoldenLittleEndian(expected, 72, status.diagnosticsRevision);
    writeGoldenLittleEndian(expected, 80, status.desiredWidth);
    writeGoldenLittleEndian(expected, 84, status.desiredHeight);
    assertTrue(encoded == expected,
            "VWS1 native encoding must match all 96 golden bytes");
}

void testRenderProfilingWireLayoutMatchesGoldenBytes() {
    NativeRenderProfilingSnapshot snapshot;
    snapshot.resetGeneration = 17;
    snapshot.enabled = true;
    snapshot.callbackCount = 18;
    snapshot.presentedCount = 19;
    snapshot.deferredCount = 20;
    snapshot.failedCount = 21;
    snapshot.elapsedNanos = 22;
    snapshot.cpuFrame = {30, 31, 32, 33, 34};
    snapshot.frameInterval = {40, 41, 42, 43, 44};
    snapshot.cameraLatency = {50, 51, 52, 53, 54};

    std::array<std::uint8_t,
            cellularity::app::render_worker::kNativeRenderProfilingSize>
            encoded{};
    cellularity::app::render_worker::encodeNativeRenderProfilingSnapshot(
            snapshot, encoded.data());
    decltype(encoded) expected{};
    writeGoldenLittleEndian(expected, 0,
            cellularity::app::render_worker::kNativeRenderProfilingMagic);
    writeGoldenLittleEndian(expected, 4,
            cellularity::app::render_worker::kNativeRenderProfilingVersion);
    writeGoldenLittleEndian(expected, 6,
            static_cast<std::uint16_t>(expected.size()));
    writeGoldenLittleEndian(expected, 8, snapshot.resetGeneration);
    writeGoldenLittleEndian(expected, 16, UINT32_C(1));
    writeGoldenLittleEndian(expected, 24, snapshot.callbackCount);
    writeGoldenLittleEndian(expected, 32, snapshot.presentedCount);
    writeGoldenLittleEndian(expected, 40, snapshot.deferredCount);
    writeGoldenLittleEndian(expected, 48, snapshot.failedCount);
    writeGoldenLittleEndian(expected, 56, snapshot.elapsedNanos);
    writeGoldenDistribution(expected, 64, snapshot.cpuFrame);
    writeGoldenDistribution(expected, 104, snapshot.frameInterval);
    writeGoldenDistribution(expected, 144, snapshot.cameraLatency);
    assertTrue(encoded == expected,
            "VRP1 native encoding must match all 184 golden bytes");
}

void testDormantCloseCreatesNoSession() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Dormant,
            "Dormant workers must reject frame requests");
    auto pending = surface(3);
    assertTrue(fixture.worker.attachSurface(std::move(pending), 10, 10, 1)
                    == WorkerCommandAdmission::Dormant,
            "Dormant workers must reject surfaces");
    stopWorker(fixture.worker);
    assertTrue(fixture.backend.createCount == 0,
            "Close-before-start must not create a Vulkan session");
}

void testPendingFrameOpensAfterResumeAndSurface() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.attachSurface(surface(4), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Attach-before-resume must remain pending");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Request-before-resume must remain pending");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must open the frame gate");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Pending frame must present once after both gates open");
    const auto status = fixture.worker.status();
    assertTrue(status.phase == NativeVulkanWorkerPhase::Ready
                    && status.appliedSurfaceGeneration == 1,
            "Successful attach must publish Ready with its generation");
    stopWorker(fixture.worker);
    assertTrue(fixture.backend.createThread == fixture.backend.destroyThread
                    && fixture.backend.createThread
                            == fixture.backend.lastOperationThread
                    && fixture.source->affinityValid,
            "Every backend/source operation must stay on one owner thread");
}

void testCoalescesFrameRequestsWhileFrameIsBlocked() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(40), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must become ready before the blocked frame");

    fixture.source->blockNextFrame();
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "First frame request must be accepted");
    assertTrue(fixture.source->waitForFrame(2s),
            "First frame must enter the blocking source");
    for (int request = 0; request < 100; ++request) {
        const auto admission = fixture.worker.requestFrame();
        assertTrue(admission == FrameRequestAdmission::Accepted
                        || admission == FrameRequestAdmission::Coalesced,
                "Requests submitted during a frame must remain admissible");
    }
    fixture.source->releaseFrame();
    assertTrue(waitUntil([&] {
        return fixture.source->frameCount.load() == 2
                && fixture.worker.status().presentedFrames == 2;
    }),
            "Requests submitted during a frame must coalesce into one follow-up");
    std::this_thread::sleep_for(20ms);
    assertTrue(fixture.source->frameCount.load() == 2,
            "Coalesced requests must not leak extra frames");
    stopWorker(fixture.worker);
}

void testReadyWorkerBlocksUntilExplicitFrameRequest() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(41), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must become ready");
    std::this_thread::sleep_for(30ms);
    assertTrue(fixture.source->frameCount.load() == 0,
            "A ready worker must not render without a request");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Explicit frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Explicit request must render exactly once");
    stopWorker(fixture.worker);
}

void testResizeDuringBlockedAttachUsesLatestExtent() {
    WorkerFixture fixture;
    fixture.backend.blockNextAttach();
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(42), 1, 1, 1)
                    == WorkerCommandAdmission::Accepted,
            "Initial Surface must be accepted");
    assertTrue(fixture.backend.waitForAttach(2s),
            "Physical attach must enter the blocking backend");
    assertTrue(fixture.worker.resizeSurface(109, 119, 1)
                    == WorkerCommandAdmission::Accepted,
            "Resize during physical attach must be accepted");
    fixture.backend.releaseAttach();
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready
                && fixture.source->lastWidth == 109
                && fixture.source->lastHeight == 119;
    }),
            "Blocked attach must converge on the newest queued dimensions");
    assertTrue(fixture.backend.attachCount.load() == 1
                    && fixture.backend.resizeCount.load() == 1,
            "Resize during attach must reuse the consumed Surface reference");
    stopWorker(fixture.worker);
}

void testBlockedResizePublishesOnlyNewestExtent() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(51), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must become ready");

    fixture.backend.blockNextResize();
    assertTrue(fixture.worker.resizeSurface(400, 300, 1)
                    == WorkerCommandAdmission::Accepted,
            "First resize must be accepted");
    assertTrue(fixture.backend.waitForResize(2s),
            "First resize must enter the blocking backend");
    for (int index = 0; index < 100; ++index) {
        assertTrue(fixture.worker.resizeSurface(index + 10, index + 20, 1)
                        == WorkerCommandAdmission::Accepted,
                "Queued resize must be accepted");
    }
    fixture.backend.releaseResize();
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready
                && fixture.source->lastWidth == 109
                && fixture.source->lastHeight == 119;
    }),
            "Blocked resize must converge on the latest dimensions");
    assertTrue(fixture.backend.resizeCount.load() == 2,
            "Only the in-flight and newest resize may reach the backend");
    assertTrue(fixture.source->resizeCount.load() == 1,
            "A stale in-flight resize must not publish old source dimensions");
    stopWorker(fixture.worker);
}

void testAppliedResizeInvalidatesStalePresentedOutcome() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Worker must resume");
    assertTrue(fixture.worker.attachSurface(surface(0xB1), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Initial Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase
                == NativeVulkanWorkerPhase::Ready;
    }),
            "Initial Surface must become ready");

    fixture.source->blockNextFrame();
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Old-size frame must be accepted");
    assertTrue(fixture.source->waitForFrame(2s),
            "Old-size frame must enter the source");
    assertTrue(fixture.worker.resizeSurface(640, 480, 1)
                    == WorkerCommandAdmission::Accepted,
            "Resize during an in-flight frame must be accepted");
    fixture.source->releaseFrame();

    assertTrue(waitUntil([&] {
        const auto status = fixture.worker.status();
        return status.phase == NativeVulkanWorkerPhase::Ready
                && status.desiredWidth == 640 && status.desiredHeight == 480
                && status.presentedFrames == 1
                && (status.flags & cellularity::app::render_worker::kWorkerHasFrameOutcome)
                == 0;
    }),
            "Applying the resize must invalidate the old-size Presented outcome");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Current-size frame must be accepted");
    assertTrue(waitUntil([&] {
        const auto status = fixture.worker.status();
        return status.presentedFrames == 2
                && (status.flags & cellularity::app::render_worker::kWorkerHasFrameOutcome)
                != 0
                && status.lastFrameOutcome
                == static_cast<std::uint32_t>(
                        RenderFrameOutcome::Presented);
    }),
            "Only a post-resize frame may publish a current Presented outcome");
    stopWorker(fixture.worker);
}

void testReplacementDuringBlockedAttachKeepsOnlyNewestSurface() {
    WorkerFixture fixture;
    fixture.backend.blockNextAttach();
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(43), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Initial Surface must be accepted");
    assertTrue(fixture.backend.waitForAttach(2s),
            "Initial attach must enter the blocking backend");
    assertTrue(fixture.worker.detachSurface(1)
                    == WorkerCommandAdmission::Accepted,
            "Current generation detach must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(44), 640, 480, 2)
                    == WorkerCommandAdmission::Accepted,
            "Replacement Surface must be accepted");
    assertTrue(fixture.worker.resizeSurface(900, 700, 2)
                    == WorkerCommandAdmission::Accepted,
            "Latest replacement extent must be accepted");
    fixture.backend.releaseAttach();
    assertTrue(waitUntil([&] {
        const auto status = fixture.worker.status();
        return status.phase == NativeVulkanWorkerPhase::Ready
                && status.appliedSurfaceGeneration == 2
                && fixture.source->lastGeneration == 2
                && fixture.source->lastWidth == 900
                && fixture.source->lastHeight == 700;
    }),
            "Only the newest replacement Surface may become observable");
    assertTrue(fixture.backend.attachCount.load() == 2
                    && fixture.backend.detachCount.load() == 1
                    && fixture.source->attachCount.load() == 1,
            "Stale physical success must roll back before the replacement binds");
    stopWorker(fixture.worker);
}

void testCloseWaitsForOutstandingFrame() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(45), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must become ready");
    fixture.source->blockNextFrame();
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame must be accepted");
    assertTrue(fixture.source->waitForFrame(2s),
            "Frame must enter the blocking source");
    fixture.worker.requestStop();
    assertTrue(fixture.worker.awaitStopped(20ms) == AwaitStoppedResult::TimedOut,
            "Join must wait for the owner-thread frame to return");
    fixture.source->releaseFrame();
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Worker must stop after the frame returns");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Stopped worker binding must finalize");
    assertTrue(fixture.backend.destroyCount.load() == 1,
            "Backend session must be destroyed once after the frame");
}

void testStaleGenerationCannotDetachReplacementSurface() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(46), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Initial Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Initial Surface must become ready");
    assertTrue(fixture.worker.attachSurface(surface(47), 640, 480, 2)
                    == WorkerCommandAdmission::Accepted,
            "Replacement Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().appliedSurfaceGeneration == 2;
    }),
            "Replacement generation must become active");
    const int detachCount = fixture.backend.detachCount.load();
    assertTrue(fixture.worker.detachSurface(1)
                    == WorkerCommandAdmission::StaleGeneration,
            "Old generation detach must be rejected");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Replacement Surface must remain renderable");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Replacement Surface must present after stale detach");
    assertTrue(fixture.backend.detachCount.load() == detachCount,
            "Stale detach must not reach the backend");
    stopWorker(fixture.worker);
}

void testZeroExtentSurfaceRendersAfterPositiveResize() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(48), 0, 0, 1)
                    == WorkerCommandAdmission::Accepted,
            "Zero-extent Surface must remain pending");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must remain pending behind zero extent");
    std::this_thread::sleep_for(20ms);
    assertTrue(fixture.backend.attachCount.load() == 0
                    && fixture.source->frameCount.load() == 0,
            "Zero extent must not attach or render");
    assertTrue(fixture.worker.resizeSurface(1080, 2400, 1)
                    == WorkerCommandAdmission::Accepted,
            "Positive resize must open the Surface gate");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Pending request must render after positive resize");
    assertTrue(fixture.source->lastWidth == 1080
                    && fixture.source->lastHeight == 2400,
            "Source must receive the positive dimensions");
    stopWorker(fixture.worker);
}

void testFallbackPresentationCompletesWithoutRetry() {
    WorkerFixture fixture;
    fixture.source->enqueueFrame(cellularity::render::makeRenderFrameResult(
            RenderFrameOutcome::Presented,
            cellularity::render::kFallbackClearPresented));
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(49), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Fallback clear must count as a completed presentation");
    std::this_thread::sleep_for(20ms);
    assertTrue(fixture.source->frameCount.load() == 1,
            "Fallback presentation must not schedule a retry");
    stopWorker(fixture.worker);
}

void testInvalidFrameResultIsTerminalContractFailure() {
    WorkerFixture fixture;
    fixture.source->enqueueFrame({
            RenderFrameOutcome::Presented,
            UINT32_C(0x80000000),
            0,
    });
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(50), 320, 240, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase
                == NativeVulkanWorkerPhase::TerminalFailure;
    }),
            "Invalid frame result must fail closed");
    assertTrue(fixture.worker.status().failure
                    == NativeVulkanWorkerFailure::InvariantViolation,
            "Invalid frame result must preserve the contract failure code");
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Terminal worker must exit");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Terminal worker binding must finalize");
}

void testDeferredStartupBindingWaitsForNativeWake() {
    WorkerFixture fixture;
    fixture.source->bindResult.store(
            SourceSurfaceResult::DeferredRebind,
            std::memory_order_release);
    const auto wake = fixture.worker.rebindWakeControl().lock();
    assertTrue(wake != nullptr,
            "Worker must expose a lifetime-safe native rebind wake");
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start while its source binding is deferred");
    assertTrue(waitUntil([&] {
        return fixture.source->bindCount.load() == 1;
    }),
            "Deferred startup binding must be attempted once");
    std::this_thread::sleep_for(30ms);
    assertTrue(fixture.source->bindCount.load() == 1,
            "Deferred startup binding must wait without timer polling");

    fixture.source->bindResult.store(
            SourceSurfaceResult::Applied,
            std::memory_order_release);
    wake->notifyRebindReady();
    assertTrue(waitUntil([&] {
        return fixture.source->bindCount.load() == 2;
    }),
            "Native rebind wake must retry the deferred startup binding");
    stopWorker(fixture.worker);
}

void testDeferredShutdownBindingWaitsForNativeWake() {
    WorkerFixture fixture;
    const auto wake = fixture.worker.rebindWakeControl().lock();
    assertTrue(wake != nullptr,
            "Worker must retain its native rebind wake through shutdown");
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start before shutdown binding is deferred");
    assertTrue(waitUntil([&] {
        return fixture.source->bindCount.load() == 1;
    }),
            "Worker source must bind before shutdown");

    fixture.source->unbindResult.store(
            SourceSurfaceResult::DeferredRebind,
            std::memory_order_release);
    fixture.worker.requestStop();
    assertTrue(waitUntil([&] {
        return fixture.source->unbindCount.load() == 1;
    }),
            "Shutdown must attempt to remove the source binding");
    std::this_thread::sleep_for(30ms);
    assertTrue(fixture.source->unbindCount.load() == 1,
            "Deferred shutdown binding must wait without timer polling");

    fixture.source->unbindResult.store(
            SourceSurfaceResult::Applied,
            std::memory_order_release);
    wake->notifyRebindReady();
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "A native rebind wake must let shutdown finish");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Shutdown binding must finalize after the deferred unbind");
}

void testDeferredShutdownSurfaceDetachWaitsForNativeWake() {
    WorkerFixture fixture;
    const auto wake = fixture.worker.rebindWakeControl().lock();
    assertTrue(wake != nullptr,
            "Worker must retain its native rebind wake through shutdown");
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(60), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must become ready");
    fixture.source->detachResult.store(
            SourceSurfaceResult::DeferredRebind,
            std::memory_order_release);
    fixture.worker.requestStop();
    assertTrue(waitUntil([&] {
        return fixture.source->detachCount.load() >= 2;
    }),
            "Shutdown must attempt bounded logical Surface teardown");
    assertTrue(fixture.worker.awaitStopped(20ms) == AwaitStoppedResult::TimedOut,
            "Shutdown must not unbind the backend before logical detach");
    fixture.source->detachResult.store(
            SourceSurfaceResult::Applied, std::memory_order_release);
    wake->notifyRebindReady();
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Native rebind wake must complete ordered shutdown detach");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Shutdown binding must finalize after ordered detach");
    assertTrue(fixture.source->unbindCount.load() == 1,
            "Backend binding must unbind after logical detach completes");
}

void testPauseRetiresSurfaceAndRequiresResubmit() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(5), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Initial Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must attach before pause");
    assertTrue(fixture.worker.pause() == WorkerCommandAdmission::Accepted,
            "Pause must be accepted");
    const bool paused = waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Paused
                && fixture.backend.detachCount == 1;
    });
    if (!paused) {
        const auto failedStatus = fixture.worker.status();
        std::cerr << "pause phase=" << static_cast<int>(failedStatus.phase)
                  << " flags=" << failedStatus.flags
                  << " failure=" << static_cast<int>(failedStatus.failure)
                  << " backend_detach=" << fixture.backend.detachCount
                  << " source_detach=" << fixture.source->detachCount << '\n';
    }
    assertTrue(paused, "Pause must detach the applied backend Surface");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume after pause must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "A frame request must remain pending without a Surface");
    std::this_thread::sleep_for(10ms);
    assertTrue(fixture.worker.status().presentedFrames == 0,
            "Resume without a resubmitted Surface must not draw");
    assertTrue(fixture.worker.attachSurface(surface(6), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Lifecycle Surface resubmission must be accepted");
    const bool resumedFrame = waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    });
    if (!resumedFrame) {
        const auto failedStatus = fixture.worker.status();
        std::cerr << "resubmit phase=" << static_cast<int>(failedStatus.phase)
                  << " flags=" << failedStatus.flags
                  << " failure=" << static_cast<int>(failedStatus.failure)
                  << " backend_attach=" << fixture.backend.attachCount
                  << " backend_detach=" << fixture.backend.detachCount
                  << " source_attach=" << fixture.source->attachCount
                  << " source_frames=" << fixture.source->frameCount << '\n';
    }
    assertTrue(resumedFrame,
            "Equal-generation lifecycle resubmit must be accepted");
    stopWorker(fixture.worker);
}

void testOnDemandRetryIsNativeAndBounded() {
    WorkerFixture fixture;
    fixture.source->enqueue(RenderFrameOutcome::Deferred);
    fixture.source->enqueue(RenderFrameOutcome::Presented);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(7), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "On-demand deferred frame must retry without another caller request");
    assertTrue(fixture.source->frameCount == 2,
            "A deferred frame must coalesce into one native retry");
    stopWorker(fixture.worker);
}

void testContinuousDeferredFrameWaitsForExternalCadence() {
    WorkerFixture fixture(FramePacingMode::ExternallyPacedContinuous);
    fixture.source->enqueue(RenderFrameOutcome::Deferred);
    fixture.source->enqueue(RenderFrameOutcome::Presented);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(52), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.source->frameCount.load() == 1;
    }),
            "First continuous frame must execute");
    std::this_thread::sleep_for(30ms);
    assertTrue(fixture.source->frameCount.load() == 1,
            "Continuous pacing must not create a native retry timer");
    assertTrue((fixture.worker.status().flags
                       & cellularity::app::render_worker::
                               kWorkerPacedRetryPending)
                    == 0,
            "Continuous pacing must not publish a retry bit");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Next external cadence request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Next external cadence request must present");
    stopWorker(fixture.worker);
}

void testSourceDeferredFrameWaitsForRebindWake() {
    WorkerFixture fixture;
    const auto wake = fixture.worker.rebindWakeControl().lock();
    assertTrue(wake != nullptr,
            "Worker must expose a native rebind wake");
    fixture.source->sourceFrameStatus.store(
            SourceFrameStatus::DeferredRebind, std::memory_order_release);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(53), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.source->frameCount.load() >= 1;
    }),
            "Deferred source frame must be attempted");
    std::this_thread::sleep_for(30ms);
    assertTrue(fixture.source->frameCount.load() == 1,
            "Deferred source frame must wait for a native rebind wake");
    fixture.source->sourceFrameStatus.store(
            SourceFrameStatus::Completed, std::memory_order_release);
    wake->notifyRebindReady();
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Rebind wake must replay the preserved frame request once");
    assertTrue(fixture.source->frameCount.load() == 2,
            "Rebind replay must not duplicate the frame");
    stopWorker(fixture.worker);
}

void testDemoHudUpdatesWithoutRecreatingSession() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(54), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.setDemoPerformanceHudEnabled(true)
                    == WorkerCommandAdmission::Accepted,
            "Demo HUD enable must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "First frame must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "First frame must present");
    assertTrue(fixture.worker.setDemoPerformanceHudEnabled(false)
                    == WorkerCommandAdmission::Accepted,
            "Demo HUD disable must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Second frame must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 2;
    }),
            "Second frame must present");
    {
        std::lock_guard<std::mutex> lock(fixture.source->mutex);
        assertTrue(fixture.source->hudStates
                        == std::vector<bool>({true, false}),
                "Demo source must observe the latest HUD state per frame");
    }
    assertTrue(fixture.backend.createCount.load() == 1,
            "HUD changes must not recreate the backend session");
    stopWorker(fixture.worker);
}

void testPauseWhileAttachIsBlockedPreventsLateActivation() {
    WorkerFixture fixture;
    fixture.backend.blockNextAttach();
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(55), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.backend.waitForAttach(2s),
            "Physical attach must enter the blocking backend");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted before pause");
    assertTrue(fixture.worker.pause() == WorkerCommandAdmission::Accepted,
            "Pause must be accepted while attach is blocked");
    fixture.backend.releaseAttach();
    assertTrue(waitUntil([&] {
        return fixture.backend.detachCount.load() == 1
                && fixture.worker.status().phase
                == NativeVulkanWorkerPhase::Paused;
    }),
            "Late physical attach must roll back into Paused");
    std::this_thread::sleep_for(20ms);
    assertTrue(fixture.source->attachCount.load() == 0
                    && fixture.source->frameCount.load() == 0,
            "Paused stale attach must not reach the source or renderer");
    stopWorker(fixture.worker);
}

void testRepeatedSurfaceCyclesKeepOneSessionAndBalancedCallbacks() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    for (std::uint64_t generation = 1; generation <= 32; ++generation) {
        assertTrue(fixture.worker.attachSurface(
                           surface(100 + generation), 100, 80, generation)
                        == WorkerCommandAdmission::Accepted,
                "Cycle Surface must be accepted");
        assertTrue(waitUntil([&] {
            return fixture.worker.status().appliedSurfaceGeneration
                    == generation;
        }),
                "Cycle Surface must become active");
        assertTrue(fixture.worker.detachSurface(generation)
                        == WorkerCommandAdmission::Accepted,
                "Cycle detach must be accepted");
        assertTrue(waitUntil([&] {
            return fixture.worker.status().appliedSurfaceGeneration == 0;
        }),
                "Cycle Surface must detach");
    }
    stopWorker(fixture.worker);
    assertTrue(fixture.backend.createCount.load() == 1
                    && fixture.backend.destroyCount.load() == 1,
            "Repeated Surface cycles must retain one backend session");
    if (fixture.backend.attachCount.load() != 32
            || fixture.backend.detachCount.load() != 32
            || fixture.source->attachCount.load() != 32
            || fixture.source->detachCount.load() != 32) {
        std::cerr << "cycle backend_attach="
                  << fixture.backend.attachCount.load()
                  << " backend_detach=" << fixture.backend.detachCount.load()
                  << " source_attach=" << fixture.source->attachCount.load()
                  << " source_detach=" << fixture.source->detachCount.load()
                  << '\n';
    }
    assertTrue(fixture.backend.attachCount.load() == 32
                    && fixture.backend.detachCount.load() == 32
                    && fixture.source->attachCount.load() == 32
                    && fixture.source->detachCount.load() == 32,
            "Repeated Surface callbacks must remain balanced");
}

void testRecoverableFailureNeedsReplacementSurface() {
    WorkerFixture fixture;
    fixture.source->enqueue(RenderFrameOutcome::Failed);
    fixture.source->enqueue(RenderFrameOutcome::Presented);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(8), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase
                == NativeVulkanWorkerPhase::RecoverableFailure;
    }),
            "Failed frame must latch a recoverable failure");
    assertTrue(fixture.worker.requestFrame()
                    == FrameRequestAdmission::FailureLatched,
            "A recoverable failure must reject ordinary frame requests");
    assertTrue(fixture.worker.attachSurface(surface(9), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Replacement Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request after recovery must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Explicit replacement Surface must recover the same session");
    assertTrue(fixture.backend.createCount == 1,
            "Recoverable failure must not recreate the Vulkan session");
    stopWorker(fixture.worker);
}

void testRecoverableAttachFailureReusesSessionAfterReplacement() {
    WorkerFixture fixture;
    fixture.backend.operationStatus.store(
            CELLULARITY_VULKAN_OPERATION_FAILED_V1,
            std::memory_order_release);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(57), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Failing Surface must still be admitted");
    assertTrue(waitUntil([&] {
        const auto status = fixture.worker.status();
        return status.phase == NativeVulkanWorkerPhase::RecoverableFailure
                && status.failure
                == NativeVulkanWorkerFailure::SurfaceAttachFailed;
    }),
            "Backend attach failure must latch a recoverable failure");
    assertTrue(fixture.worker.requestFrame()
                    == FrameRequestAdmission::FailureLatched,
            "Frames must remain blocked until explicit replacement");

    fixture.backend.operationStatus.store(
            CELLULARITY_VULKAN_OPERATION_OK_V1,
            std::memory_order_release);
    assertTrue(fixture.worker.attachSurface(surface(58), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Equal-generation replacement must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Successful replacement must clear recoverable attach failure");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Recovered Surface must accept a frame");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Recovered Surface must present");
    assertTrue(fixture.backend.createCount.load() == 1,
            "Recoverable attach failure must retain one backend session");
    stopWorker(fixture.worker);
}

void testDeferredSourceDetachWaitsForRebindWake() {
    WorkerFixture fixture;
    const auto wake = fixture.worker.rebindWakeControl().lock();
    assertTrue(wake != nullptr,
            "Worker must expose a native rebind wake");
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(59), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase == NativeVulkanWorkerPhase::Ready;
    }),
            "Surface must become ready");
    fixture.source->detachResult.store(
            SourceSurfaceResult::DeferredRebind,
            std::memory_order_release);
    assertTrue(fixture.worker.detachSurface(1)
                    == WorkerCommandAdmission::Accepted,
            "Detach must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.source->detachCount.load() == 2
                && fixture.backend.detachCount.load() == 1;
    }),
            "Physical detach must finish and bounded logical replay may run once");
    std::this_thread::sleep_for(30ms);
    assertTrue(fixture.source->detachCount.load() == 2,
            "Deferred logical detach must not timer-poll");
    fixture.source->detachResult.store(
            SourceSurfaceResult::Applied, std::memory_order_release);
    wake->notifyRebindReady();
    assertTrue(waitUntil([&] {
        return fixture.source->detachCount.load() == 3
                && (fixture.worker.status().flags
                           & cellularity::app::render_worker::
                                   kWorkerSourceSurfaceApplied)
                == 0;
    }),
            "Native wake must complete the deferred logical detach");
    stopWorker(fixture.worker);
}

void testTransientOutcomesRetryOnlyOncePerOutcome() {
    const std::array<RenderFrameOutcome, 2> outcomes{
            RenderFrameOutcome::SwapchainRecreated,
            RenderFrameOutcome::SkippedNotReady,
    };
    std::uintptr_t identity = 200;
    for (const RenderFrameOutcome outcome : outcomes) {
        WorkerFixture fixture;
        fixture.source->enqueue(outcome);
        fixture.source->enqueue(RenderFrameOutcome::Presented);
        assertTrue(fixture.worker.start() == StartWorkerResult::Started,
                "Worker must start");
        assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
                "Resume must be accepted");
        assertTrue(fixture.worker.attachSurface(
                           surface(identity++), 100, 80, 1)
                        == WorkerCommandAdmission::Accepted,
                "Surface must be accepted");
        assertTrue(fixture.worker.requestFrame()
                        == FrameRequestAdmission::Accepted,
                "Frame request must be accepted");
        assertTrue(waitUntil([&] {
            return fixture.worker.status().presentedFrames == 1;
        }),
                "Transient outcome must retry and then present");
        assertTrue(fixture.source->frameCount.load() == 2,
                "Transient outcome must produce one bounded retry");
        const std::uint64_t expectedSwapchains =
                outcome == RenderFrameOutcome::SwapchainRecreated ? 1 : 0;
        assertTrue(fixture.worker.status().swapchainRecreations
                        == expectedSwapchains,
                "Swapchain counter must match the transient outcome");
        stopWorker(fixture.worker);
    }
}

void testSurfaceLossSignalsAndReattaches() {
    WorkerFixture fixture;
    fixture.source->enqueue(RenderFrameOutcome::SurfaceLost);
    fixture.source->enqueue(RenderFrameOutcome::Presented);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(10), 100, 80, 4)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        if (fixture.worker.status().phase
                != NativeVulkanWorkerPhase::SurfaceLost) {
            return false;
        }
        std::lock_guard<std::mutex> lock(fixture.signal->mutex);
        return !fixture.signal->actionBits.empty();
    }),
            "Surface loss must wake the managed host");
    assertTrue(fixture.worker.attachSurface(surface(11), 100, 80, 4)
                    == WorkerCommandAdmission::Accepted,
            "Same-generation replacement Surface must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().presentedFrames == 1;
    }),
            "Same-generation holder Surface must recover after loss");
    stopWorker(fixture.worker);
}

void testSourceFailureIsTerminal() {
    WorkerFixture fixture;
    fixture.source->sourceFrameStatus = SourceFrameStatus::Failed;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(12), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase
                == NativeVulkanWorkerPhase::TerminalFailure;
    }),
            "Source frame failure must stop recovery attempts");
    assertTrue(fixture.worker.status().failure
                    == NativeVulkanWorkerFailure::SourceFrameFailed,
            "Terminal source failure must preserve its typed code");
    fixture.worker.requestStop();
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Terminal worker must still join");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Terminal worker must finalize before destruction");
}

void testStatusSignalRetriesOnce() {
    WorkerFixture fixture;
    fixture.signal->results.push_back(StatusSignalDelivery::RetryableFailure);
    fixture.signal->results.push_back(StatusSignalDelivery::Accepted);
    fixture.source->sourceFrameStatus = SourceFrameStatus::Failed;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(13), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        std::lock_guard<std::mutex> lock(fixture.signal->mutex);
        return fixture.signal->deliveryTimes.size() == 2;
    }),
            "Retryable status delivery must perform one bounded retry");
    {
        std::lock_guard<std::mutex> lock(fixture.signal->mutex);
        assertTrue(
                fixture.signal->deliveryTimes[1]
                                - fixture.signal->deliveryTimes[0]
                        >= 20ms,
                "Status delivery retry must not spin immediately");
    }
    fixture.worker.requestStop();
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Worker must exit after the terminal signal is delivered");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Worker binding must finalize after status retry");
}

void testRepeatedStatusSignalFailureFailsClosed() {
    WorkerFixture fixture;
    fixture.signal->results.push_back(StatusSignalDelivery::RetryableFailure);
    fixture.signal->results.push_back(StatusSignalDelivery::RetryableFailure);
    fixture.source->sourceFrameStatus.store(
            SourceFrameStatus::Failed, std::memory_order_release);
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Resume must be accepted");
    assertTrue(fixture.worker.attachSurface(surface(56), 100, 80, 1)
                    == WorkerCommandAdmission::Accepted,
            "Surface must be accepted");
    assertTrue(fixture.worker.requestFrame() == FrameRequestAdmission::Accepted,
            "Frame request must be accepted");
    assertTrue(waitUntil([&] {
        return (fixture.worker.status().flags
                       & cellularity::app::render_worker::
                               kWorkerStatusSignalDeliveryFailed)
                != 0;
    }),
            "Second signal transport failure must become sticky");
    assertTrue(fixture.worker.status().failure
                    == NativeVulkanWorkerFailure::SourceFrameFailed,
            "Signal failure must not overwrite an earlier terminal cause");
    assertTrue(fixture.worker.awaitStopped(2s) == AwaitStoppedResult::Stopped,
            "Worker must exit after bounded signal retries");
    assertTrue(fixture.worker.finalizeBindingAfterJoin(),
            "Failed worker binding must finalize");
}

void testExplicitDiagnosticsRunOnOwnerThreadAndEnforceSequence() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start for explicit diagnostics");
    assertTrue(waitUntil([&] {
        return fixture.source->bindCount.load() == 1;
    }),
            "Worker must bind before explicit diagnostics");

    assertTrue(fixture.worker.requestDiagnostics(2)
                    == DiagnosticsRequestAdmission::InvalidRequestId,
            "A diagnostic request gap must be rejected");
    assertTrue(fixture.worker.requestDiagnostics(1)
                    == DiagnosticsRequestAdmission::Accepted,
            "The first exact diagnostic request id must be accepted");
    assertTrue(fixture.worker.requestDiagnostics(2)
                    == DiagnosticsRequestAdmission::Busy,
            "A second diagnostic request must not replace the active slot");

    std::uint64_t revision = 0;
    const std::string report = waitForDiagnosticsReport(
            fixture.worker, 1, 0, revision);
    assertTrue(revision != 0
                    && revision != std::numeric_limits<std::uint64_t>::max(),
            "Explicit diagnostics must use an ordinary non-zero revision");
    assertTrue(report.find("fake_backend_diagnostics=ready")
                    != std::string::npos,
            "Explicit diagnostics must contain the backend owner-thread report");
    assertTrue(fixture.backend.diagnosticsAffinityValid.load(),
            "Every diagnostics callback must run on the Vulkan owner thread");
    assertTrue(fixture.worker.releaseDiagnostics(1, revision + 1)
                    == DiagnosticsReleaseResult::StaleRevision,
            "A mismatched explicit revision must not release the slot");
    assertTrue(fixture.worker.releaseDiagnostics(1, revision)
                    == DiagnosticsReleaseResult::Released,
            "The completed explicit report must release exactly once");
    assertTrue(fixture.worker.releaseDiagnostics(1, revision)
                    == DiagnosticsReleaseResult::AlreadyReleased,
            "Repeated release must use the bounded retired high-water mark");
    assertTrue(fixture.worker.requestDiagnostics(1)
                    == DiagnosticsRequestAdmission::InvalidRequestId,
            "A replayed request id must be rejected");

    fixture.backend.blockNextDiagnostics();
    assertTrue(fixture.worker.requestDiagnostics(2)
                    == DiagnosticsRequestAdmission::Accepted,
            "The next exact request id must be accepted");
    assertTrue(fixture.backend.waitForDiagnostics(2s),
            "The owner thread must enter the blocked diagnostics callback");
    assertTrue(fixture.worker.releaseDiagnostics(2, 0)
                    == DiagnosticsReleaseResult::CancellationRequested,
            "Release while collecting must request asynchronous cancellation");
    fixture.backend.releaseBlockedDiagnostics();
    std::array<std::uint8_t, 8> ignored{};
    assertTrue(waitUntil([&] {
        return fixture.worker.readDiagnosticsChunk(
                                     2, 0, 0, ignored.data(), ignored.size())
                       .status
                == DiagnosticsChunkReadStatus::Cancelled;
    }),
            "Cancelled diagnostics must retire after the owner callback returns");
    assertTrue(fixture.worker.releaseDiagnostics(2, 0)
                    == DiagnosticsReleaseResult::AlreadyReleased,
            "A retired cancellation must be idempotent");
    assertTrue(fixture.worker.requestDiagnostics(4)
                    == DiagnosticsRequestAdmission::InvalidRequestId,
            "A later diagnostic request gap must remain rejected");
    assertTrue(fixture.backend.diagnosticsSizeCount.load() == 2
                    && fixture.backend.diagnosticsReleaseCount.load() == 2,
            "Every acquired backend snapshot must be released exactly once");
    stopWorker(fixture.worker);
}

void testRecoverableFailureFreezesBackendDiagnosticsBeforeSignal() {
    WorkerFixture fixture;
    assertTrue(fixture.worker.start() == StartWorkerResult::Started,
            "Worker must start for failure diagnostics");
    assertTrue(fixture.worker.resume() == WorkerCommandAdmission::Accepted,
            "Worker must resume for failure diagnostics");
    fixture.backend.operationStatus = CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    assertTrue(fixture.worker.attachSurface(surface(0xD1), 640, 360, 1)
                    == WorkerCommandAdmission::Accepted,
            "Failing test surface must still enter the worker");
    assertTrue(waitUntil([&] {
        return fixture.worker.status().phase
                == NativeVulkanWorkerPhase::RecoverableFailure;
    }),
            "Backend attachment failure must become recoverable");

    const auto status = fixture.worker.status();
    assertTrue(status.diagnosticsRevision != 0
                    && status.diagnosticsRevision
                            != std::numeric_limits<std::uint64_t>::max(),
            "Recoverable failure must publish an ordinary diagnostics revision");
    std::uint64_t observedRevision = status.diagnosticsRevision;
    const std::string report = waitForDiagnosticsReport(
            fixture.worker, 0, status.diagnosticsRevision, observedRevision);
    assertTrue(report.find("failure_code=") != std::string::npos
                    && report.find("fake_backend_diagnostics=ready")
                            != std::string::npos,
            "Failure diagnostics must combine worker and backend detail");
    assertTrue(fixture.backend.diagnosticsAffinityValid.load(),
            "Failure diagnostics must preserve backend thread affinity");
    assertTrue(fixture.backend.diagnosticsSizeCount.load() == 1
                    && fixture.backend.diagnosticsReleaseCount.load() == 1,
            "Failure snapshot must be acquired and released exactly once");
    stopWorker(fixture.worker);
}

} // namespace

int main() {
    testInheritedNiceNormalizationPolicyAndFailures();
    testHostSchedulingAdapterDoesNotChangeRunnerNice();
    testSchedulingNormalizationPrecedesSessionCreation();
    testFailedSessionStartupDoesNotRepeatSchedulingNormalization();
    testSurfaceLeaseOwnership();
    testWorkerStatusWireLayoutMatchesGoldenBytes();
    testRenderProfilingWireLayoutMatchesGoldenBytes();
    testDormantCloseCreatesNoSession();
    testPendingFrameOpensAfterResumeAndSurface();
    testCoalescesFrameRequestsWhileFrameIsBlocked();
    testReadyWorkerBlocksUntilExplicitFrameRequest();
    testResizeDuringBlockedAttachUsesLatestExtent();
    testBlockedResizePublishesOnlyNewestExtent();
    testAppliedResizeInvalidatesStalePresentedOutcome();
    testReplacementDuringBlockedAttachKeepsOnlyNewestSurface();
    testCloseWaitsForOutstandingFrame();
    testStaleGenerationCannotDetachReplacementSurface();
    testZeroExtentSurfaceRendersAfterPositiveResize();
    testFallbackPresentationCompletesWithoutRetry();
    testInvalidFrameResultIsTerminalContractFailure();
    testDeferredStartupBindingWaitsForNativeWake();
    testDeferredShutdownBindingWaitsForNativeWake();
    testDeferredShutdownSurfaceDetachWaitsForNativeWake();
    testPauseRetiresSurfaceAndRequiresResubmit();
    testOnDemandRetryIsNativeAndBounded();
    testContinuousDeferredFrameWaitsForExternalCadence();
    testSourceDeferredFrameWaitsForRebindWake();
    testDemoHudUpdatesWithoutRecreatingSession();
    testPauseWhileAttachIsBlockedPreventsLateActivation();
    testRepeatedSurfaceCyclesKeepOneSessionAndBalancedCallbacks();
    testRecoverableFailureNeedsReplacementSurface();
    testRecoverableAttachFailureReusesSessionAfterReplacement();
    testDeferredSourceDetachWaitsForRebindWake();
    testTransientOutcomesRetryOnlyOncePerOutcome();
    testSurfaceLossSignalsAndReattaches();
    testSourceFailureIsTerminal();
    testStatusSignalRetriesOnce();
    testRepeatedStatusSignalFailureFailsClosed();
    testExplicitDiagnosticsRunOnOwnerThreadAndEnforceSequence();
    testRecoverableFailureFreezesBackendDiagnosticsBeforeSignal();
    std::cout << "Native Vulkan render worker tests passed\n";
    return 0;
}
