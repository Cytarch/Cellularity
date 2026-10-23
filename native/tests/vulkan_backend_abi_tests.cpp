#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"
#include "render/backend/abi/VulkanBackendAbiError.h"
#include "render/backend/abi/VulkanBackendAbiValidation.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"
#include "render/frame/PreparedRenderFrame.h"
#include "tests/support/TestAssertions.h"

struct CellularityVulkanSessionOpaque {
    void* context = nullptr;
};

namespace {

using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderFrameOutcome;
using cellularity::render::RenderPresentationState;
using cellularity::render::RenderViewportState;
using cellularity::render::abi::VulkanBackendSession;
using cellularity::render::abi::VulkanRenderSurfaceSessionAdapter;
using cellularity::tests::assertThrows;
using cellularity::tests::assertTrue;

static_assert(std::is_standard_layout_v<CellularityVulkanSurfaceApiV1>);
static_assert(std::is_trivially_copyable_v<CellularityVulkanSurfaceApiV1>);
static_assert(std::is_standard_layout_v<CellularityPreparedRenderFrameViewV1>);
static_assert(std::is_trivially_copyable_v<CellularityPreparedRenderFrameViewV1>);
static_assert(std::is_standard_layout_v<CellularityVulkanErrorV1>);
static_assert(std::is_trivially_copyable_v<CellularityVulkanErrorV1>);
static_assert(sizeof(CellularityVulkanErrorV1) == 536);

struct FakeBackend final {
    CellularityVulkanSessionOpaque session{this};
    int sequence = 0;
    int retainCount = 0;
    int releaseCount = 0;
    int createCount = 0;
    int destroyCount = 0;
    int attachCount = 0;
    int consumedWindowReferences = 0;
    int presentCount = 0;
    int clearCount = 0;
    int destroySequence = 0;
    int releaseSequence = 0;
    bool failCreate = false;
    bool invalidFailureRecord = false;
    bool invalidSuccessRecord = false;
    bool dirtySuccessTail = false;
    bool createInfoValid = false;
    bool geometryPointersBorrowed = false;
    bool ringGeometryTransferred = false;
    std::uint32_t attachedValue = 1;
    CellularityVulkanOperationStatusV1 operationStatus = CELLULARITY_VULKAN_OPERATION_OK_V1;
    CellularityVulkanSurfaceOutcomeV1 surfaceOutcome = CELLULARITY_VULKAN_SURFACE_PRESENTED_V1;
    const float* expectedTrianglePointer = nullptr;
    const float* expectedColorPointer = nullptr;
    const std::uint16_t* expectedIndexPointer = nullptr;
    const std::int32_t* expectedBunchPointer = nullptr;
    std::uint64_t expectedSourceIdentity = 0;
};

FakeBackend* gBackend = nullptr;

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
    auto& fake = *static_cast<FakeBackend*>(context);
    ++fake.releaseCount;
    fake.releaseSequence = ++fake.sequence;
}

CellularityVulkanSessionHandle createSession(const CellularityVulkanSessionCreateInfoV1* info, CellularityVulkanErrorV1* error) {
    FakeBackend& fake = *gBackend;
    ++fake.createCount;
    initializeSuccess(error);
    fake.createInfoValid = info != nullptr && info->abi_version == CELLULARITY_VULKAN_ABI_VERSION_V1 && info->struct_size == CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE && info->asset_manager != nullptr && info->flags == 0 && info->reserved_zero == 0;
    if (fake.failCreate) {
        error->code = CELLULARITY_VULKAN_ERROR_BACKEND_INITIALIZATION_FAILED_V1;
        constexpr char message[] = "expected fake creation failure";
        std::memcpy(error->message, message, sizeof(message));
        error->message_length = sizeof(message) - 1;
        return nullptr;
    }
    if (fake.invalidFailureRecord) {
        return nullptr;
    }
    if (fake.invalidSuccessRecord) {
        error->code = CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1;
    }
    if (fake.dirtySuccessTail) {
        error->message[sizeof(error->message) - 1] = 'x';
    }
    return &fake.session;
}

void destroySession(CellularityVulkanSessionHandle session) {
    FakeBackend& fake = backend(session);
    ++fake.destroyCount;
    fake.destroySequence = ++fake.sequence;
}

CellularityVulkanOperationStatusV1 hasAttachedSurface(CellularityVulkanSessionHandle session, std::uint32_t* outAttached) {
    FakeBackend& fake = backend(session);
    *outAttached = fake.attachedValue;
    return fake.operationStatus;
}

CellularityVulkanOperationStatusV1 attachWindow(CellularityVulkanSessionHandle session, ANativeWindow*, std::int32_t, std::int32_t) {
    FakeBackend& fake = backend(session);
    ++fake.attachCount;
    ++fake.consumedWindowReferences;
    return fake.operationStatus;
}

CellularityVulkanOperationStatusV1 resizeSurface(CellularityVulkanSessionHandle session, std::int32_t, std::int32_t) {
    return backend(session).operationStatus;
}

CellularityVulkanOperationStatusV1 detachSurface(CellularityVulkanSessionHandle session) {
    return backend(session).operationStatus;
}

CellularityVulkanSurfaceOutcomeV1 presentMicroscope(CellularityVulkanSessionHandle session, const CellularityVulkanPresentationStateV1*, const CellularityPreparedRenderFrameViewV1* frame, const CellularityVulkanDebugHudViewV1*) {
    FakeBackend& fake = backend(session);
    ++fake.presentCount;
    fake.geometryPointersBorrowed = frame != nullptr && frame->source_identity == fake.expectedSourceIdentity && frame->cell_triangle_coords.data == fake.expectedTrianglePointer && frame->cell_colors.data == fake.expectedColorPointer && frame->cell_indices.data == fake.expectedIndexPointer && frame->bunches.data == fake.expectedBunchPointer;
    fake.ringGeometryTransferred = frame != nullptr && frame->environment.dish_radius == 1.0 && frame->environment.dish_inner_radius == 0.5;
    return fake.surfaceOutcome;
}

CellularityVulkanSurfaceOutcomeV1 clearSurface(CellularityVulkanSessionHandle session, const CellularityVulkanViewportStateV1*, const float[4]) {
    FakeBackend& fake = backend(session);
    ++fake.clearCount;
    return fake.surfaceOutcome;
}

CellularityVulkanSurfaceOutcomeV1 drawEnvironmentFieldPreview(CellularityVulkanSessionHandle session, const CellularityVulkanEnvironmentFieldPreviewCommandV1*) {
    return backend(session).surfaceOutcome;
}

CellularityVulkanOperationStatusV1 beginGenomeFrame(CellularityVulkanSessionHandle session, std::int32_t, std::int32_t, std::uint32_t) {
    return backend(session).operationStatus;
}

CellularityVulkanOperationStatusV1 drawGenomeGrid(CellularityVulkanSessionHandle session, const CellularityVulkanGenomeGridCommandV1*) {
    return backend(session).operationStatus;
}

CellularityVulkanOperationStatusV1 drawGenomeTree(CellularityVulkanSessionHandle session, const CellularityVulkanPresentationStateV1*, const CellularityPreparedRenderFrameViewV1*, const float[16], CellularityVulkanFloatSpanV1, const CellularityVulkanGenomeDrawOptionsV1*) {
    return backend(session).operationStatus;
}

CellularityVulkanOperationStatusV1 drawGenomeCells(CellularityVulkanSessionHandle session, const CellularityVulkanPresentationStateV1*, const CellularityPreparedRenderFrameViewV1*, const CellularityVulkanGenomeDrawOptionsV1*) {
    return backend(session).operationStatus;
}

CellularityVulkanSurfaceOutcomeV1 endGenomeSurfaceFrame(CellularityVulkanSessionHandle session) {
    return backend(session).surfaceOutcome;
}

CellularityVulkanOperationStatusV1 cancelGenomeFrame(CellularityVulkanSessionHandle session) {
    return backend(session).operationStatus;
}

CellularityVulkanOperationStatusV1 beginOffscreenRgba(CellularityVulkanSessionHandle session, CellularityVulkanOffscreenTargetV1, std::int32_t, std::int32_t) {
    return backend(session).operationStatus;
}

CellularityVulkanPassStatusV1 drawMicroscopeOffscreenPass(CellularityVulkanSessionHandle, const CellularityVulkanPresentationStateV1*, const CellularityPreparedRenderFrameViewV1*) {
    return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
}

CellularityVulkanPassStatusV1 endGenomeOffscreenPass(CellularityVulkanSessionHandle) {
    return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
}

CellularityVulkanPassStatusV1 finishOffscreenRgba(CellularityVulkanSessionHandle, CellularityVulkanOffscreenTargetV1, std::uint8_t*, std::uint64_t destinationSize, std::uint64_t* outBytesWritten) {
    *outBytesWritten = destinationSize;
    return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
}

CellularityVulkanOperationStatusV1 cancelOffscreenRgba(CellularityVulkanSessionHandle session, CellularityVulkanOffscreenTargetV1) {
    return backend(session).operationStatus;
}

CellularityVulkanOperationStatusV1 setProfilingEnabled(CellularityVulkanSessionHandle session, std::uint32_t) {
    return backend(session).operationStatus;
}

CellularityVulkanDiagnosticsStatusV1 getDiagnosticsSize(CellularityVulkanSessionHandle, std::uint64_t* outRevision, std::uint64_t* outTotalBytes) {
    *outRevision = 7;
    *outTotalBytes = 3;
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanDiagnosticsStatusV1 readDiagnosticsChunk(CellularityVulkanSessionHandle, std::uint64_t, std::uint64_t, std::uint8_t*, std::uint32_t destinationSize, std::uint32_t* outBytesWritten) {
    *outBytesWritten = destinationSize;
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanDiagnosticsStatusV1 releaseDiagnosticsSnapshot(CellularityVulkanSessionHandle, std::uint64_t) {
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanSurfaceApiV1 makeApi(FakeBackend& fake) {
    gBackend = &fake;
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
            beginOffscreenRgba,
            drawMicroscopeOffscreenPass,
            endGenomeOffscreenPass,
            finishOffscreenRgba,
            cancelOffscreenRgba,
            setProfilingEnabled,
            getDiagnosticsSize,
            readDiagnosticsChunk,
            releaseDiagnosticsSnapshot
    };
}

AAssetManager* fakeAssetManager() {
    return reinterpret_cast<AAssetManager*>(static_cast<std::uintptr_t>(0x1000));
}

void testManifestValidationRejectsBeforeCallbacks() {
    FakeBackend fake;
    auto api = makeApi(fake);

    api.abi_version = 2;
    assertThrows([&] {
        (void)VulkanBackendSession::create(&api, fakeAssetManager());
    },
            "Unknown API version must be rejected");
    api = makeApi(fake);
    api.struct_size = CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE - 1;
    assertThrows([&] {
        (void)VulkanBackendSession::create(&api, fakeAssetManager());
    },
            "Short API prefix must be rejected");
    api = makeApi(fake);
    api.api_context = nullptr;
    assertThrows([&] {
        (void)VulkanBackendSession::create(&api, fakeAssetManager());
    },
            "Null API context must be rejected");
    api = makeApi(fake);
    api.present_microscope = nullptr;
    assertThrows([&] {
        (void)VulkanBackendSession::create(&api, fakeAssetManager());
    },
            "Null required callback must be rejected");
    api = makeApi(fake);
    assertThrows([&] {
        (void)VulkanBackendSession::create(&api, nullptr);
    },
            "Null asset manager must be rejected");

    assertTrue(fake.retainCount == 0 && fake.createCount == 0, "Rejected manifests must not invoke backend callbacks");
}

void testCreationFailuresBalanceApiAndSessionOwnership() {
    FakeBackend failed;
    failed.failCreate = true;
    auto failedApi = makeApi(failed);
    assertThrows([&] {
        (void)VulkanBackendSession::create(&failedApi, fakeAssetManager());
    },
            "Backend creation failure must propagate");
    assertTrue(failed.retainCount == 1 && failed.releaseCount == 1 && failed.createCount == 1 && failed.destroyCount == 0, "Failed creation must balance only the API lease");

    FakeBackend invalidFailure;
    invalidFailure.invalidFailureRecord = true;
    auto invalidFailureApi = makeApi(invalidFailure);
    assertThrows([&] {
        (void)VulkanBackendSession::create(&invalidFailureApi, fakeAssetManager());
    },
            "Null session with a success code must be an ABI contract failure");
    assertTrue(invalidFailure.retainCount == 1 && invalidFailure.releaseCount == 1 && invalidFailure.destroyCount == 0, "Invalid failure records must still balance the API lease");

    FakeBackend malformed;
    malformed.invalidSuccessRecord = true;
    auto malformedApi = makeApi(malformed);
    assertThrows([&] {
        (void)VulkanBackendSession::create(&malformedApi, fakeAssetManager());
    },
            "Malformed success record must fail closed");
    assertTrue(malformed.retainCount == 1 && malformed.releaseCount == 1 && malformed.createCount == 1 && malformed.destroyCount == 1 && malformed.destroySequence < malformed.releaseSequence, "Malformed success must destroy the opaque session before releasing its API lease");

    FakeBackend dirtySuccess;
    dirtySuccess.dirtySuccessTail = true;
    auto dirtySuccessApi = makeApi(dirtySuccess);
    assertThrows([&] {
        (void)VulkanBackendSession::create(&dirtySuccessApi, fakeAssetManager());
    },
            "Success must return a fully zeroed error payload");
    assertTrue(dirtySuccess.destroyCount == 1 && dirtySuccess.releaseCount == 1, "Dirty success records must not leak the created session or API lease");
}

void testHappyPathBorrowsFrameAndClosesOnOwnerThread() {
    FakeBackend fake;
    auto api = makeApi(fake);
    api.struct_size = sizeof(api) + 128;
    std::shared_ptr<VulkanBackendSession> session = VulkanBackendSession::create(&api, fakeAssetManager());
    assertTrue(fake.createInfoValid && fake.retainCount == 1 && fake.createCount == 1, "Compatible API tails must be ignored and create-info must be canonical");

    {
        VulkanRenderSurfaceSessionAdapter adapter(session);
        VulkanRenderSurfaceSessionAdapter secondAdapter(session);
        assertTrue(adapter.bindingIdentity() == session->bindingIdentity()
                        && secondAdapter.bindingIdentity()
                                == adapter.bindingIdentity(),
                "Adapters over one backend session must preserve one binding identity");
        PreparedRenderFrame frame;
        frame.valid = true;
        frame.geometryReady = true;
        frame.environment.dishRadius = 1.0;
        frame.environment.dishInnerRadius = 0.5;
        frame.cellTriangleCoords = {1.0f, 2.0f, 3.0f};
        frame.cellColors = {0.1f, 0.2f, 0.3f, 1.0f};
        frame.cellIndices = {0, 1, 2};
        frame.bunches[0] = 1;
        fake.expectedTrianglePointer = frame.cellTriangleCoords.data();
        fake.expectedColorPointer = frame.cellColors.data();
        fake.expectedIndexPointer = frame.cellIndices.data();
        fake.expectedBunchPointer = reinterpret_cast<const std::int32_t*>(frame.bunches.data());
        fake.expectedSourceIdentity = reinterpret_cast<std::uintptr_t>(&frame);

        RenderPresentationState presentation;
        const auto result = adapter.presentMicroscope(presentation, frame);
        assertTrue(result.outcome == RenderFrameOutcome::Presented && fake.presentCount == 1 && fake.geometryPointersBorrowed, "Steady frame calls must borrow the producer geometry without a copy");
        assertTrue(fake.ringGeometryTransferred, "Both substrate radii cross the Vulkan ABI boundary");

        fake.operationStatus = CELLULARITY_VULKAN_OPERATION_FAILED_V1;
        const auto* window = reinterpret_cast<ANativeWindow*>(static_cast<std::uintptr_t>(0x2000));
        assertTrue(session->attachWindow(const_cast<ANativeWindow*>(window), 320, 240) == CELLULARITY_VULKAN_OPERATION_FAILED_V1 && fake.attachCount == 1 && fake.consumedWindowReferences == 1, "attach_window must consume exactly one reference on failure");

        fake.surfaceOutcome = UINT32_C(0xFFFFFFFF);
        RenderViewportState viewport;
        const std::array<float, 4> color{};
        assertThrows([&] {
            (void)adapter.clearSurface(viewport, color);
        },
                "Unknown wire outcome must fail as an ABI contract error");

        fake.operationStatus = CELLULARITY_VULKAN_OPERATION_OK_V1;
        fake.attachedValue = 2;
        std::uint32_t attached = 1;
        assertTrue(session->hasAttachedSurface(attached)
                                == CELLULARITY_VULKAN_OPERATION_FAILED_V1
                        && attached == 0,
                "Non-boolean attached state must fail closed");
    }

    bool wrongThreadRejected = false;
    std::thread wrongThread([&] {
        try {
            session->closeOnOwnerThread();
        } catch (const std::logic_error&) {
            wrongThreadRejected = true;
        }
    });
    wrongThread.join();
    assertTrue(wrongThreadRejected && fake.destroyCount == 0, "A non-owner thread must not destroy Vulkan state");

    session->closeOnOwnerThread();
    assertTrue(session->phase() == VulkanBackendSession::Phase::Closed && fake.destroyCount == 1 && fake.releaseCount == 1 && fake.destroySequence < fake.releaseSequence, "Owner close must destroy the opaque session before releasing the API table");
    std::uint32_t attached = 1;
    assertTrue(session->hasAttachedSurface(attached) == CELLULARITY_VULKAN_OPERATION_FAILED_V1 && attached == 0, "Closed sessions must reject stale calls without entering the backend");
}

void testWireStatusValidation() {
    using cellularity::render::abi::isKnownVulkanDiagnosticsStatus;
    using cellularity::render::abi::isKnownVulkanOperationStatus;
    using cellularity::render::abi::isKnownVulkanPassStatus;
    using cellularity::render::abi::isKnownVulkanSurfaceOutcome;

    assertTrue(isKnownVulkanOperationStatus(CELLULARITY_VULKAN_OPERATION_FAILED_V1) && !isKnownVulkanOperationStatus(4), "Operation status range must be closed");
    assertTrue(isKnownVulkanSurfaceOutcome(CELLULARITY_VULKAN_SURFACE_FAILED_V1) && !isKnownVulkanSurfaceOutcome(6), "Surface outcome range must be closed");
    assertTrue(isKnownVulkanPassStatus(CELLULARITY_VULKAN_PASS_FAILED_V1) && !isKnownVulkanPassStatus(2), "Pass status range must be closed");
    assertTrue(isKnownVulkanDiagnosticsStatus(CELLULARITY_VULKAN_DIAGNOSTICS_REVISION_EXHAUSTED_V1) && !isKnownVulkanDiagnosticsStatus(7), "Diagnostics status range must be closed");
}

void testBorrowedSpanValidationFailsClosed() {
    using cellularity::render::abi::validation::validInt32Matrix;
    using cellularity::render::abi::validation::validSpan;

    alignas(float) std::array<std::uint8_t, 32> storage{};
    const auto* aligned = reinterpret_cast<const float*>(storage.data());
    const auto* misaligned = reinterpret_cast<const float*>(storage.data() + 1);
    assertTrue(validSpan<float>(nullptr, 0, sizeof(float), alignof(float)), "An empty canonical span may have a null pointer");
    assertTrue(!validSpan<float>(nullptr, 1, sizeof(float), alignof(float)), "A non-empty span must have storage");
    assertTrue(!validSpan<float>(misaligned, 1, sizeof(float), alignof(float)), "A misaligned span must be rejected");
    assertTrue(!validSpan<float>(aligned, 1, sizeof(double), alignof(float)), "A span with the wrong element size must be rejected");
    assertTrue(!validSpan<float>(aligned, 1, sizeof(float), 1), "A span with the wrong declared alignment must be rejected");
    assertTrue(!validSpan<float>(aligned, UINT64_MAX, sizeof(float), alignof(float)), "A byte-size-overflowing span must be rejected");

    std::array<std::int32_t, CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1 * CELLULARITY_VULKAN_MAXIMUM_DRAW_BUNCHES_V1> matrixStorage{};
    CellularityVulkanInt32MatrixViewV1 matrix{
            matrixStorage.data(),
            CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1,
            CELLULARITY_VULKAN_MAXIMUM_DRAW_BUNCHES_V1,
            CELLULARITY_VULKAN_MAXIMUM_DRAW_BUNCHES_V1,
            sizeof(std::int32_t),
            alignof(std::int32_t),
            0
    };
    assertTrue(validInt32Matrix(matrix), "A canonical fixed draw matrix must be accepted");
    matrix.row_stride_elements = matrix.columns - 1;
    assertTrue(!validInt32Matrix(matrix), "A matrix row stride shorter than its row must be rejected");
    matrix.row_stride_elements = matrix.columns;
    matrix.reserved_zero = 1;
    assertTrue(!validInt32Matrix(matrix), "A matrix with unknown fields must be rejected");
}

void testErrorRecordKeepsUtf8BoundaryAndZeroTail() {
    using cellularity::render::abi::error::initializeError;
    using cellularity::render::abi::error::setError;

    CellularityVulkanErrorV1 error;
    std::memset(&error, 0xA5, sizeof(error));
    std::string message(510, 'a');
    message.append("\xE2\x82\xAC");
    setError(&error, CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1,
            message.c_str(), -7);
    assertTrue(error.abi_version == CELLULARITY_VULKAN_ABI_VERSION_V1
                    && error.struct_size == sizeof(error)
                    && error.code == CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1
                    && error.backend_code == -7
                    && error.flags
                            == CELLULARITY_VULKAN_ERROR_MESSAGE_TRUNCATED_V1
                    && error.message_length == 510
                    && error.message[510] == '\0',
            "Truncated errors must end before a partial UTF-8 code point");
    assertTrue(std::all_of(error.message + 510,
                       error.message + sizeof(error.message),
                       [](char byte) {
                           return byte == '\0';
                       }),
            "Error truncation must leave the unused output tail zeroed");

    std::memset(&error, 0xA5, sizeof(error));
    initializeError(&error);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&error);
    assertTrue(error.abi_version == CELLULARITY_VULKAN_ABI_VERSION_V1
                    && error.struct_size == sizeof(error)
                    && std::all_of(bytes + 8, bytes + sizeof(error),
                            [](unsigned char byte) {
                                return byte == 0;
                            }),
            "Success initialization must deterministically clear all payload bytes");
}

} // namespace

int main() {
    testManifestValidationRejectsBeforeCallbacks();
    testCreationFailuresBalanceApiAndSessionOwnership();
    testHappyPathBorrowsFrameAndClosesOnOwnerThread();
    testWireStatusValidation();
    testBorrowedSpanValidationFailsClosed();
    testErrorRecordKeepsUtf8BoundaryAndZeroTail();
    std::cout << "Vulkan backend ABI tests passed\n";
    return 0;
}
