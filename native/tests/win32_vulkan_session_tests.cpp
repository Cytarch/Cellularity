// Windows Vulkan backend session contract.
//
// The presentation-path test proves a Win32 surface can be presented to. This one
// goes further and stands the actual session up on the live driver: it reaches the
// backend through the same C entry point the managed layer uses, points the asset
// source at the compiled SPIR-V and the source textures, and requires the session
// to initialize.
//
// Initialization is the load-bearing part. A session that comes up has already
// created an instance, chosen a physical device and queue family, created the
// device, loaded and validated every shader module it needs at start-up, and built
// the pipelines that depend on them. A shader that failed to compile, a module
// built for the wrong target environment, or an asset path that resolves to
// nothing all surface here rather than at the first frame.
//
// An off-screen capture is then opened and cancelled, which exercises the target's
// image, memory and render-pass creation without depending on frame content. It
// requires no window and shows nothing.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"
#include "tests/support/TestAssertions.h"
#include "tests/support/Win32ContractWindow.h"

namespace {

using cellularity::tests::assertTrue;
using cellularity::tests::OffscreenWin32Window;

// The extent the session is asked to present at. It matches the window's nominal
// size rather than its frame-adjusted client area, because attachWindow takes the
// extent explicitly and the session creates its swapchain from that.
constexpr std::int32_t kSurfaceWidth = 640;
constexpr std::int32_t kSurfaceHeight = 480;

// The backend is reached through its C API rather than by constructing its session
// class, because that is the surface the managed layer and any future host shell
// will use. When the backend is linked in, the entry point is an ordinary symbol.
const CellularityVulkanSurfaceApiV1* surfaceApi() {
    return cellularity_vulkan_get_surface_api_v1();
}

// The session create-info is versioned by size, so the caller states how much of it
// it knows about. The full size is used here, which asks for the asset roots.
CellularityVulkanSessionCreateInfoV1 makeCreateInfo(
        const std::string& assetRoot, const std::string& assetRootSecondary) {
    CellularityVulkanSessionCreateInfoV1 createInfo{};
    createInfo.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    createInfo.struct_size = CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_FULL_SIZE;
    createInfo.asset_manager = nullptr;
    createInfo.asset_source = CELLULARITY_VULKAN_ASSET_SOURCE_FILESYSTEM_V1;
    createInfo.reserved_zero = 0;
    createInfo.asset_root = assetRoot.c_str();
    createInfo.asset_root_secondary =
            assetRootSecondary.empty() ? nullptr : assetRootSecondary.c_str();
    return createInfo;
}

void verifyApiTableIsReachable() {
    const CellularityVulkanSurfaceApiV1* api = surfaceApi();
    assertTrue(api != nullptr,
            "The backend must export its v1 surface API table");
    assertTrue(api->abi_version == CELLULARITY_VULKAN_ABI_VERSION_V1,
            "The exported table must declare the ABI version this test speaks");

    // Spot-check the entries the session lifecycle needs. A table that is present
    // but partly empty would fail much later and less clearly.
    assertTrue(api->create_session != nullptr && api->destroy_session != nullptr,
            "The API table must provide the session lifecycle");
    assertTrue(api->has_attached_surface != nullptr
                    && api->attach_window != nullptr
                    && api->detach_surface != nullptr,
            "The API table must provide the surface lifecycle");
    assertTrue(api->begin_offscreen_rgba != nullptr
                    && api->cancel_offscreen_rgba != nullptr,
            "The API table must provide the off-screen capture lifecycle");
    std::cout << "api_table_version=" << api->abi_version << "\n";
}

// The operation wrappers collapse every thrown exception into a single failed
// status, so the backend's own recorded reason is the only place the cause
// survives. Reading it is what turns "it failed" into something actionable.
std::string readSessionDiagnostics(const CellularityVulkanSurfaceApiV1* api,
        CellularityVulkanSessionHandle session) {
    if (api->get_diagnostics_size == nullptr
            || api->read_diagnostics_chunk == nullptr) {
        return {};
    }
    std::uint64_t revision = 0;
    std::uint64_t totalBytes = 0;
    if (api->get_diagnostics_size(session, &revision, &totalBytes)
                    != CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1
            || totalBytes == 0) {
        return {};
    }
    // The chunk reader takes a 32-bit size, so a large report is read in pieces.
    std::string report;
    report.reserve(static_cast<std::size_t>(totalBytes));
    std::uint64_t offset = 0;
    while (offset < totalBytes) {
        const std::uint32_t remaining =
                static_cast<std::uint32_t>(std::min<std::uint64_t>(
                        totalBytes - offset, 64 * 1024));
        std::vector<std::uint8_t> chunk(remaining);
        std::uint32_t written = 0;
        if (api->read_diagnostics_chunk(session, revision, offset, chunk.data(),
                    remaining, &written)
                != CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1) {
            break;
        }
        if (written == 0) {
            break;
        }
        report.append(reinterpret_cast<const char*>(chunk.data()), written);
        offset += written;
    }
    return report;
}

void verifySessionInitializes(const std::string& assetRoot,
        const std::string& assetRootSecondary) {
    const CellularityVulkanSurfaceApiV1* api = surfaceApi();
    const CellularityVulkanSessionCreateInfoV1 createInfo =
            makeCreateInfo(assetRoot, assetRootSecondary);

    // The backend reports why it refused, which is the difference between a
    // missing shader and a missing device.
    CellularityVulkanErrorV1 error{};
    error.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    error.struct_size = sizeof(error);
    CellularityVulkanSessionHandle session =
            api->create_session(&createInfo, &error);
    if (session == nullptr) {
        std::cerr << "session_create_error_code="
                  << static_cast<int>(error.code)
                  << " backend_code=" << error.backend_code
                  << " message="
                  << std::string(error.message,
                             error.message + (error.message_length < sizeof(error.message)
                                                      ? error.message_length
                                                      : sizeof(error.message)))
                  << "\n";
        assertTrue(false,
                "The session must initialize with the compiled shader assets at '"
                        + assetRoot + "'");
    }

    std::uint32_t attached = 0;
    const CellularityVulkanOperationStatusV1 attachedStatus =
            api->has_attached_surface(session, &attached);
    assertTrue(attachedStatus == CELLULARITY_VULKAN_OPERATION_OK_V1,
            "has_attached_surface must succeed on a live session");
    assertTrue(attached == 0,
            "A session created without a window must report no attached surface");

    // The device is created when a window is attached, not when the session is
    // created, because device selection depends on the surface: the queue family
    // has to be able to present to it. Everything that touches the device
    // therefore happens after this call.
    OffscreenWin32Window window;
    const CellularityVulkanOperationStatusV1 attachStatus = api->attach_window(
            session, window.handle(), kSurfaceWidth, kSurfaceHeight);
    assertTrue(attachStatus == CELLULARITY_VULKAN_OPERATION_OK_V1,
            "attach_window must succeed for a real HWND, got "
                    + std::to_string(static_cast<int>(attachStatus))
                    + "; backend diagnostics: "
                    + readSessionDiagnostics(api, session));

    assertTrue(api->has_attached_surface(session, &attached)
                            == CELLULARITY_VULKAN_OPERATION_OK_V1
                    && attached == 1,
            "The session must report an attached surface after attach_window");

    // Opening and cancelling a capture exercises the off-screen target's image,
    // memory and render pass. Cancelling rather than reading back keeps this test
    // independent of frame content, which the frame contract tests cover.
    const CellularityVulkanOperationStatusV1 beginStatus = api->begin_offscreen_rgba(
            session, CELLULARITY_VULKAN_OFFSCREEN_SUBSTRATE_PREVIEW_V1, 64,
            64);
    assertTrue(beginStatus == CELLULARITY_VULKAN_OPERATION_OK_V1,
            "begin_offscreen_rgba must succeed on an initialized session, got "
                    + std::to_string(static_cast<int>(beginStatus))
                    + "; backend diagnostics: "
                    + readSessionDiagnostics(api, session));

    const CellularityVulkanOperationStatusV1 cancelStatus =
            api->cancel_offscreen_rgba(
                    session, CELLULARITY_VULKAN_OFFSCREEN_SUBSTRATE_PREVIEW_V1);
    assertTrue(cancelStatus == CELLULARITY_VULKAN_OPERATION_OK_V1,
            "cancel_offscreen_rgba must succeed for an active capture");

    api->destroy_session(session);
    std::cout << "session_initialized=1\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        // The asset roots are passed in so the test does not have to guess where
        // the build put the generated SPIR-V or where the source textures live.
        std::string assetRoot = argc > 1 ? argv[1] : std::string();
        std::string assetRootSecondary = argc > 2 ? argv[2] : std::string();
        if (assetRoot.empty()) {
            const char* fromEnvironment =
                    std::getenv("CELLULARITY_VULKAN_TEST_ASSET_ROOT");
            if (fromEnvironment != nullptr) {
                assetRoot = fromEnvironment;
            }
        }
        if (assetRootSecondary.empty()) {
            const char* fromEnvironment =
                    std::getenv("CELLULARITY_VULKAN_TEST_SECONDARY_ROOT");
            if (fromEnvironment != nullptr) {
                assetRootSecondary = fromEnvironment;
            }
        }
        assertTrue(!assetRoot.empty(),
                "The test needs the asset root holding vulkan/shaders/*.spv");

        verifyApiTableIsReachable();
        verifySessionInitializes(assetRoot, assetRootSecondary);
        std::cout << "win32 vulkan session contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "win32 vulkan session contract failed: " << error.what()
                  << "\n";
        return 1;
    }
}
