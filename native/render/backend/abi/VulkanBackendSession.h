#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"

namespace cellularity::render::abi {

class VulkanBackendSession final {
public:
    enum class Phase : std::uint8_t {
        Starting,
        Ready,
        Closing,
        Closed,
    };

    [[nodiscard]] static std::shared_ptr<VulkanBackendSession> create(
            const CellularityVulkanSurfaceApiV1* api,
            AAssetManager* assetManager);

    VulkanBackendSession(const VulkanBackendSession&) = delete;
    VulkanBackendSession& operator=(const VulkanBackendSession&) = delete;
    ~VulkanBackendSession();

    [[nodiscard]] Phase phase() const noexcept;
    [[nodiscard]] bool ownerThread() const noexcept;
    [[nodiscard]] const void* bindingIdentity() const noexcept;

    [[nodiscard]] CellularityVulkanOperationStatusV1 hasAttachedSurface(
            std::uint32_t& attached) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 attachWindow(
            ANativeWindow* window, std::int32_t width,
            std::int32_t height) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 resizeSurface(
            std::int32_t width, std::int32_t height) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 detachSurface() noexcept;
    [[nodiscard]] CellularityVulkanSurfaceOutcomeV1 presentMicroscope(
            const CellularityVulkanPresentationStateV1& presentation,
            const CellularityPreparedRenderFrameViewV1& frame,
            const CellularityVulkanDebugHudViewV1* debugHud) noexcept;
    [[nodiscard]] CellularityVulkanSurfaceOutcomeV1 clearSurface(
            const CellularityVulkanViewportStateV1& viewport,
            const float color[4]) noexcept;
    [[nodiscard]] CellularityVulkanSurfaceOutcomeV1
    drawEnvironmentFieldPreview(
            const CellularityVulkanEnvironmentFieldPreviewCommandV1& command)
            noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 beginGenomeFrame(
            std::int32_t width, std::int32_t height,
            std::uint32_t fullyTransparentClear) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 drawGenomeGrid(
            const CellularityVulkanGenomeGridCommandV1& command) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 drawGenomeTree(
            const CellularityVulkanPresentationStateV1& presentation,
            const CellularityPreparedRenderFrameViewV1& frame,
            const float matrix[16],
            CellularityVulkanFloatSpanV1 lineVertices,
            const CellularityVulkanGenomeDrawOptionsV1& options) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 drawGenomeCells(
            const CellularityVulkanPresentationStateV1& presentation,
            const CellularityPreparedRenderFrameViewV1& frame,
            const CellularityVulkanGenomeDrawOptionsV1& options) noexcept;
    [[nodiscard]] CellularityVulkanSurfaceOutcomeV1
    endGenomeSurfaceFrame() noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1
    cancelGenomeFrame() noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 beginOffscreenRgba(
            CellularityVulkanOffscreenTargetV1 target,
            std::int32_t width, std::int32_t height) noexcept;
    [[nodiscard]] CellularityVulkanPassStatusV1
    drawMicroscopeOffscreenPass(
            const CellularityVulkanPresentationStateV1& presentation,
            const CellularityPreparedRenderFrameViewV1& frame) noexcept;
    [[nodiscard]] CellularityVulkanPassStatusV1
    endGenomeOffscreenPass() noexcept;
    [[nodiscard]] CellularityVulkanPassStatusV1 finishOffscreenRgba(
            CellularityVulkanOffscreenTargetV1 target,
            std::uint8_t* destination,
            std::uint64_t destinationSize,
            std::uint64_t& bytesWritten) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 cancelOffscreenRgba(
            CellularityVulkanOffscreenTargetV1 target) noexcept;
    [[nodiscard]] CellularityVulkanOperationStatusV1 setProfilingEnabled(
            bool enabled) noexcept;
    [[nodiscard]] CellularityVulkanDiagnosticsStatusV1 getDiagnosticsSize(
            std::uint64_t& revision,
            std::uint64_t& totalBytes) noexcept;
    [[nodiscard]] CellularityVulkanDiagnosticsStatusV1 readDiagnosticsChunk(
            std::uint64_t revision,
            std::uint64_t offset,
            std::uint8_t* destination,
            std::uint32_t destinationSize,
            std::uint32_t& bytesWritten) noexcept;
    [[nodiscard]] CellularityVulkanDiagnosticsStatusV1
    releaseDiagnosticsSnapshot(std::uint64_t revision) noexcept;

    // Must run on the creator/owner thread after attachment users are drained.
    void closeOnOwnerThread();

private:
    VulkanBackendSession(const CellularityVulkanSurfaceApiV1& api,
            CellularityVulkanSessionHandle session,
            std::thread::id ownerThread) noexcept;

    class Call final {
    public:
        explicit Call(VulkanBackendSession& owner) noexcept;
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        ~Call();
        [[nodiscard]] explicit operator bool() const noexcept {
            return mSession != nullptr;
        }
        [[nodiscard]] CellularityVulkanSessionHandle session() const noexcept {
            return mSession;
        }

    private:
        VulkanBackendSession* mOwner = nullptr;
        CellularityVulkanSessionHandle mSession = nullptr;
    };

    [[nodiscard]] bool beginCall(
            CellularityVulkanSessionHandle& session) noexcept;
    void endCall() noexcept;

    CellularityVulkanSurfaceApiV1 mApi{};
    mutable std::mutex mMutex;
    std::condition_variable mNoCalls;
    CellularityVulkanSessionHandle mSession = nullptr;
    std::thread::id mOwnerThread;
    Phase mPhase = Phase::Starting;
    std::uint32_t mCallsInFlight = 0;
};

[[nodiscard]] bool isKnownVulkanOperationStatus(
        CellularityVulkanOperationStatusV1 status) noexcept;
[[nodiscard]] bool isKnownVulkanSurfaceOutcome(
        CellularityVulkanSurfaceOutcomeV1 outcome) noexcept;
[[nodiscard]] bool isKnownVulkanPassStatus(
        CellularityVulkanPassStatusV1 status) noexcept;
[[nodiscard]] bool isKnownVulkanDiagnosticsStatus(
        CellularityVulkanDiagnosticsStatusV1 status) noexcept;

} // namespace cellularity::render::abi
