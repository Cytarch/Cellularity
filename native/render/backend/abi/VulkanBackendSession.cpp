#include "render/backend/abi/VulkanBackendSession.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>

namespace cellularity::render::abi {
namespace {

static_assert(sizeof(CellularityVulkanErrorV1) == 536);
static_assert(offsetof(CellularityVulkanErrorV1, abi_version) == 0);
static_assert(offsetof(CellularityVulkanErrorV1, struct_size) == 4);
static_assert(offsetof(CellularityVulkanErrorV1, code) == 8);
static_assert(offsetof(CellularityVulkanErrorV1, flags) == 12);
static_assert(offsetof(CellularityVulkanErrorV1, backend_code) == 16);
static_assert(offsetof(CellularityVulkanErrorV1, message_length) == 20);
static_assert(offsetof(CellularityVulkanErrorV1, message) == 24);
static_assert((sizeof(void*) == 8
                      && CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE == 224
                      && CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE
                              == 24)
        || (sizeof(void*) == 4
                && CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE == 116
                && CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE
                        == 20));

[[nodiscard]] bool hasRequiredCallbacks(
        const CellularityVulkanSurfaceApiV1& api) noexcept {
    return api.retain_api != nullptr && api.release_api != nullptr
            && api.create_session != nullptr && api.destroy_session != nullptr
            && api.has_attached_surface != nullptr
            && api.attach_window != nullptr && api.resize_surface != nullptr
            && api.detach_surface != nullptr
            && api.present_microscope != nullptr
            && api.clear_surface != nullptr
            && api.draw_environment_field_preview != nullptr
            && api.begin_genome_frame != nullptr
            && api.draw_genome_grid != nullptr
            && api.draw_genome_tree != nullptr
            && api.draw_genome_cells != nullptr
            && api.end_genome_surface_frame != nullptr
            && api.cancel_genome_frame != nullptr
            && api.begin_offscreen_rgba != nullptr
            && api.draw_microscope_offscreen_pass != nullptr
            && api.end_genome_offscreen_pass != nullptr
            && api.finish_offscreen_rgba != nullptr
            && api.cancel_offscreen_rgba != nullptr
            && api.set_profiling_enabled != nullptr
            && api.get_diagnostics_size != nullptr
            && api.read_diagnostics_chunk != nullptr
            && api.release_diagnostics_snapshot != nullptr;
}

[[nodiscard]] std::string creationFailure(
        const CellularityVulkanErrorV1& error) {
    if (error.abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || error.struct_size != sizeof(CellularityVulkanErrorV1)
            || error.code == CELLULARITY_VULKAN_ERROR_NONE_V1
            || error.message_length >= sizeof(error.message)
            || error.message[error.message_length] != '\0'
            || (error.flags & ~CELLULARITY_VULKAN_ERROR_MESSAGE_TRUNCATED_V1)
                    != 0
            || error.code > CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1) {
        return "Vulkan backend returned an invalid creation error record";
    }
    if (error.message_length == 0) {
        return "Vulkan backend session creation failed";
    }
    return std::string(error.message, error.message_length);
}

} // namespace

bool isKnownVulkanOperationStatus(
        CellularityVulkanOperationStatusV1 status) noexcept {
    return status <= CELLULARITY_VULKAN_OPERATION_FAILED_V1;
}

bool isKnownVulkanSurfaceOutcome(
        CellularityVulkanSurfaceOutcomeV1 outcome) noexcept {
    return outcome <= CELLULARITY_VULKAN_SURFACE_FAILED_V1;
}

bool isKnownVulkanPassStatus(
        CellularityVulkanPassStatusV1 status) noexcept {
    return status <= CELLULARITY_VULKAN_PASS_FAILED_V1;
}

bool isKnownVulkanDiagnosticsStatus(
        CellularityVulkanDiagnosticsStatusV1 status) noexcept {
    return status <= CELLULARITY_VULKAN_DIAGNOSTICS_REVISION_EXHAUSTED_V1;
}

std::shared_ptr<VulkanBackendSession> VulkanBackendSession::create(
        const CellularityVulkanSurfaceApiV1* api,
        AAssetManager* assetManager) {
    if (api == nullptr) {
        throw std::invalid_argument("Vulkan backend API address is null");
    }
    if (api->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || api->struct_size
                    < CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE
            || api->api_context == nullptr || !hasRequiredCallbacks(*api)) {
        throw std::invalid_argument("Vulkan backend API V1 is incompatible");
    }
    if (assetManager == nullptr) {
        throw std::invalid_argument("Vulkan backend requires Android assets");
    }

    CellularityVulkanSurfaceApiV1 copied{};
    std::memcpy(&copied, api, sizeof(copied));
    CellularityVulkanSessionHandle session = nullptr;
    bool retained = false;
    try {
        copied.retain_api(copied.api_context);
        retained = true;

        CellularityVulkanSessionCreateInfoV1 info{};
        info.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
        info.struct_size =
                CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE;
        info.asset_manager = assetManager;

        CellularityVulkanErrorV1 error{};
        error.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
        error.struct_size = sizeof(error);
        session = copied.create_session(&info, &error);
        if (session == nullptr) {
            throw std::runtime_error(creationFailure(error));
        }
        if (error.abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
                || error.struct_size != sizeof(error)
                || error.code != CELLULARITY_VULKAN_ERROR_NONE_V1
                || error.flags != 0 || error.backend_code != 0
                || error.message_length != 0
                || !std::all_of(std::begin(error.message),
                        std::end(error.message),
                        [](char byte) {
                            return byte == '\0';
                        })) {
            const CellularityVulkanSessionHandle invalidSession = session;
            session = nullptr;
            try {
                copied.destroy_session(invalidSession);
            } catch (...) {
            }
            throw std::runtime_error(
                    "Vulkan backend returned an invalid success record");
        }
        auto result = std::shared_ptr<VulkanBackendSession>(
                new VulkanBackendSession(
                        copied, session, std::this_thread::get_id()));
        session = nullptr;
        retained = false;
        return result;
    } catch (...) {
        if (session != nullptr) {
            try {
                copied.destroy_session(session);
            } catch (...) {
            }
        }
        if (retained) {
            try {
                copied.release_api(copied.api_context);
            } catch (...) {
            }
        }
        throw;
    }
}

VulkanBackendSession::VulkanBackendSession(
        const CellularityVulkanSurfaceApiV1& api,
        CellularityVulkanSessionHandle session,
        std::thread::id ownerThread) noexcept
        : mApi(api),
          mSession(session),
          mOwnerThread(ownerThread),
          mPhase(Phase::Ready) {}

VulkanBackendSession::~VulkanBackendSession() {
    bool live = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        live = mSession != nullptr || mPhase != Phase::Closed;
    }
    if (live) {
        std::fputs("Cellularity: live Vulkan backend session reached its destructor; leaking backend resources to preserve thread affinity\n", stderr);
#ifndef NDEBUG
        assert(!live && "Vulkan backend session was not closed by its owner");
#endif
    }
}

VulkanBackendSession::Phase VulkanBackendSession::phase() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mPhase;
}

bool VulkanBackendSession::ownerThread() const noexcept {
    return std::this_thread::get_id() == mOwnerThread;
}

const void* VulkanBackendSession::bindingIdentity() const noexcept {
    return this;
}

bool VulkanBackendSession::beginCall(
        CellularityVulkanSessionHandle& session) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mPhase != Phase::Ready || mSession == nullptr || !ownerThread()) {
        return false;
    }
    ++mCallsInFlight;
    session = mSession;
    return true;
}

void VulkanBackendSession::endCall() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    assert(mCallsInFlight > 0);
    --mCallsInFlight;
    if (mCallsInFlight == 0)
        mNoCalls.notify_all();
}

VulkanBackendSession::Call::Call(VulkanBackendSession& owner) noexcept
        : mOwner(&owner) {
    if (!owner.beginCall(mSession))
        mOwner = nullptr;
}

VulkanBackendSession::Call::~Call() {
    if (mOwner != nullptr)
        mOwner->endCall();
}

#define CELLULARITY_VULKAN_INVOKE_STATUS(method, fallback, ...) \
    Call call(*this);                                           \
    if (!call)                                                  \
        return fallback;                                        \
    try {                                                       \
        return mApi.method(call.session(), __VA_ARGS__);        \
    } catch (...) {                                             \
        return fallback;                                        \
    }

CellularityVulkanOperationStatusV1 VulkanBackendSession::hasAttachedSurface(
        std::uint32_t& attached) noexcept {
    attached = 0;
    Call call(*this);
    if (!call)
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    try {
        const auto status =
                mApi.has_attached_surface(call.session(), &attached);
        if (!isKnownVulkanOperationStatus(status)
                || (status == CELLULARITY_VULKAN_OPERATION_OK_V1
                        && attached > 1)) {
            attached = 0;
            return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
        }
        if (status != CELLULARITY_VULKAN_OPERATION_OK_V1)
            attached = 0;
        return status;
    } catch (...) {
        attached = 0;
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::attachWindow(
        ANativeWindow* window, std::int32_t width,
        std::int32_t height) noexcept {
    Call call(*this);
    if (!call)
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    try {
        return mApi.attach_window(call.session(), window, width, height);
    } catch (...) {
        // The callback owns the reference as soon as it is invoked. A
        // conforming implementation catches internally; do not double free.
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::resizeSurface(
        std::int32_t width, std::int32_t height) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(resize_surface,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1, width, height)
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::detachSurface() noexcept {
    Call call(*this);
    if (!call)
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    try {
        return mApi.detach_surface(call.session());
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanSurfaceOutcomeV1 VulkanBackendSession::presentMicroscope(
        const CellularityVulkanPresentationStateV1& presentation,
        const CellularityPreparedRenderFrameViewV1& frame,
        const CellularityVulkanDebugHudViewV1* debugHud) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(present_microscope,
                UINT32_MAX, &presentation, &frame, debugHud)
}

CellularityVulkanSurfaceOutcomeV1 VulkanBackendSession::clearSurface(
        const CellularityVulkanViewportStateV1& viewport,
        const float color[4]) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(clear_surface,
                UINT32_MAX, &viewport, color)
}

CellularityVulkanSurfaceOutcomeV1
        VulkanBackendSession::drawEnvironmentFieldPreview(
                const CellularityVulkanEnvironmentFieldPreviewCommandV1& command) noexcept {
                CELLULARITY_VULKAN_INVOKE_STATUS(draw_environment_field_preview,
                        UINT32_MAX, &command)
        }

CellularityVulkanOperationStatusV1 VulkanBackendSession::beginGenomeFrame(
        std::int32_t width, std::int32_t height,
        std::uint32_t fullyTransparentClear) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(begin_genome_frame,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1,
                width, height, fullyTransparentClear)
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::drawGenomeGrid(
        const CellularityVulkanGenomeGridCommandV1& command) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(draw_genome_grid,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1, &command)
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::drawGenomeTree(
        const CellularityVulkanPresentationStateV1& presentation,
        const CellularityPreparedRenderFrameViewV1& frame,
        const float matrix[16], CellularityVulkanFloatSpanV1 lineVertices,
        const CellularityVulkanGenomeDrawOptionsV1& options) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(draw_genome_tree,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1,
                &presentation, &frame, matrix, lineVertices, &options)
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::drawGenomeCells(
        const CellularityVulkanPresentationStateV1& presentation,
        const CellularityPreparedRenderFrameViewV1& frame,
        const CellularityVulkanGenomeDrawOptionsV1& options) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(draw_genome_cells,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1,
                &presentation, &frame, &options)
}

CellularityVulkanSurfaceOutcomeV1
        VulkanBackendSession::endGenomeSurfaceFrame() noexcept {
    Call call(*this);
    if (!call)
        return UINT32_MAX;
    try {
        return mApi.end_genome_surface_frame(call.session());
    } catch (...) {
        return UINT32_MAX;
    }
}

CellularityVulkanOperationStatusV1
VulkanBackendSession::cancelGenomeFrame() noexcept {
    Call call(*this);
    if (!call)
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    try {
        return mApi.cancel_genome_frame(call.session());
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::beginOffscreenRgba(
        CellularityVulkanOffscreenTargetV1 target,
        std::int32_t width, std::int32_t height) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(begin_offscreen_rgba,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1,
                target, width, height)
}

CellularityVulkanPassStatusV1
        VulkanBackendSession::drawMicroscopeOffscreenPass(
                const CellularityVulkanPresentationStateV1& presentation,
                const CellularityPreparedRenderFrameViewV1& frame) noexcept {
                CELLULARITY_VULKAN_INVOKE_STATUS(draw_microscope_offscreen_pass,
                        UINT32_MAX, &presentation, &frame)
        }

CellularityVulkanPassStatusV1
        VulkanBackendSession::endGenomeOffscreenPass() noexcept {
    Call call(*this);
    if (!call)
        return UINT32_MAX;
    try {
        return mApi.end_genome_offscreen_pass(call.session());
    } catch (...) {
        return UINT32_MAX;
    }
}

CellularityVulkanPassStatusV1 VulkanBackendSession::finishOffscreenRgba(
        CellularityVulkanOffscreenTargetV1 target,
        std::uint8_t* destination, std::uint64_t destinationSize,
        std::uint64_t& bytesWritten) noexcept {
    bytesWritten = 0;
    CELLULARITY_VULKAN_INVOKE_STATUS(finish_offscreen_rgba, UINT32_MAX,
            target, destination, destinationSize, &bytesWritten)
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::cancelOffscreenRgba(
        CellularityVulkanOffscreenTargetV1 target) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(cancel_offscreen_rgba,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1, target)
}

CellularityVulkanOperationStatusV1 VulkanBackendSession::setProfilingEnabled(
        bool enabled) noexcept {
        CELLULARITY_VULKAN_INVOKE_STATUS(set_profiling_enabled,
                CELLULARITY_VULKAN_OPERATION_FAILED_V1,
                enabled ? UINT32_C(1) : UINT32_C(0))
}

CellularityVulkanDiagnosticsStatusV1
        VulkanBackendSession::getDiagnosticsSize(
                std::uint64_t& revision, std::uint64_t& totalBytes) noexcept {
    revision = 0;
    totalBytes = 0;
    CELLULARITY_VULKAN_INVOKE_STATUS(get_diagnostics_size,
            CELLULARITY_VULKAN_DIAGNOSTICS_FAILED_V1,
            &revision, &totalBytes)
}

CellularityVulkanDiagnosticsStatusV1
VulkanBackendSession::readDiagnosticsChunk(
        std::uint64_t revision, std::uint64_t offset,
        std::uint8_t* destination, std::uint32_t destinationSize,
        std::uint32_t& bytesWritten) noexcept {
    bytesWritten = 0;
    CELLULARITY_VULKAN_INVOKE_STATUS(read_diagnostics_chunk,
            CELLULARITY_VULKAN_DIAGNOSTICS_FAILED_V1,
            revision, offset, destination, destinationSize, &bytesWritten)
}

CellularityVulkanDiagnosticsStatusV1
VulkanBackendSession::releaseDiagnosticsSnapshot(
        std::uint64_t revision) noexcept {
    CELLULARITY_VULKAN_INVOKE_STATUS(release_diagnostics_snapshot,
            CELLULARITY_VULKAN_DIAGNOSTICS_FAILED_V1, revision)
}

#undef CELLULARITY_VULKAN_INVOKE_STATUS

void VulkanBackendSession::closeOnOwnerThread() {
    if (!ownerThread()) {
        throw std::logic_error(
                "Vulkan backend session close requires its owner thread");
    }
    CellularityVulkanSessionHandle session = nullptr;
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mPhase == Phase::Closed)
            return;
        if (mPhase != Phase::Ready) {
            throw std::logic_error("Vulkan backend session is already closing");
        }
        mPhase = Phase::Closing;
        mNoCalls.wait(lock, [this] {
            return mCallsInFlight == 0;
        });
        session = mSession;
        mSession = nullptr;
    }
    if (session != nullptr) {
        try {
            mApi.destroy_session(session);
        } catch (...) {
        }
    }
    try {
        mApi.release_api(mApi.api_context);
    } catch (...) {
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mPhase = Phase::Closed;
    }
}

} // namespace cellularity::render::abi
