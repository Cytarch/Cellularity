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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "app/interaction/NativeViewport.h"
#include "host/win32/Win32HostCamera.h"
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

// The interactive camera's projection contract.
//
// An earlier version composed this matrix from the viewport's own world matrix and a
// hand-calibrated scale, and it looked plausible while rendering the dish as an
// ellipse. Nothing failed; the frame was simply wrong. These assertions pin the two
// properties that were violated, and they need no driver, window or session.
void verifyCameraProjectionKeepsWorldShape() {
    using cellularity::host::win32::kCameraRadius;
    using cellularity::host::win32::projectionMatrixFor;

    // A dish of the fixture's radius, at the camera the fixture itself uses. The
    // fixture frames this so that the radius reaches halfway up the surface, which is
    // what makes the two projections comparable at all.
    constexpr float kDishRadius = 0.24F;
    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;

    NativeViewport viewport(kCameraRadius);
    viewport.setCameraBoundsEnabled(false);
    viewport.setSurfaceSize(kWidth, kHeight);
    viewport.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);

    const std::array<float, 16> matrix = projectionMatrixFor(viewport);

    // The world keeps its shape when one world distance covers the same number of
    // pixels whichever axis it runs along. That is a claim about pixels, not about
    // normalised coordinates: the normalised offsets of a round world are deliberately
    // unequal on a non-square surface, which is exactly what makes it round on screen.
    // An earlier version of this test asserted equality of the normalised offsets and so
    // condemned a correct projection; the fixture's own dish measures 808 by 800 pixels,
    // and this matrix reproduces the fixture's framing exactly, so equality of pixel
    // extents is the property worth pinning.
    const auto projectX = [&matrix](float worldX) {
        return matrix[0] * worldX + matrix[12];
    };
    const auto projectY = [&matrix](float worldY) {
        return matrix[5] * worldY + matrix[13];
    };
    const float worldExtent = kDishRadius;
    const float normalisedX = projectX(worldExtent) - projectX(0.0F);
    const float normalisedY = projectY(worldExtent) - projectY(0.0F);
    assertTrue(normalisedX > 0.0F && normalisedY > 0.0F,
            "A world offset must map away from the centre on both axes");

    const float halfSurfaceWidth = static_cast<float>(kWidth) * 0.5F;
    const float halfSurfaceHeight = static_cast<float>(kHeight) * 0.5F;
    const float pixelsX = normalisedX * halfSurfaceWidth;
    const float pixelsY = normalisedY * halfSurfaceHeight;
    const float pixelRatio = pixelsX / pixelsY;
    assertTrue(std::abs(pixelRatio - 1.0F) < 0.001F,
            "A circle must stay round: one world distance must cover the same number of "
            "pixels on both axes, got x/y=" + std::to_string(pixelRatio));

    // The radius must also fit inside the shorter side, or the dish would be cropped.
    const float shorterHalfExtent = std::min(halfSurfaceWidth, halfSurfaceHeight);
    assertTrue(pixelsY > 0.0F && pixelsY <= shorterHalfExtent,
            "The dish radius must fit within the surface's shorter half-extent, got "
                    + std::to_string(pixelsY) + "px of "
                    + std::to_string(shorterHalfExtent) + "px");

    // A pan is a translation and nothing else. If it moved the scale, the image would
    // zoom while being dragged.
    constexpr float kPanPixelsX = 120.0F;
    constexpr float kPanPixelsY = 90.0F;
    viewport.moveByScreenDelta(kPanPixelsX, kPanPixelsY);
    const std::array<float, 16> pannedOnce = projectionMatrixFor(viewport);
    assertTrue(pannedOnce[0] == matrix[0] && pannedOnce[5] == matrix[5],
            "A pan must not change the projection's scale");

    // Dragging must reach both axes, each from its own starting camera so the expected
    // shift is unambiguous. The camera bound is what defeated this before: it pins
    // whichever axis the view exactly fills, so a drag along that axis did nothing while
    // the other worked.
    const float pixelsPerUnit = static_cast<float>(kHeight) / viewport.viewHeight();
    constexpr float kDragPixels = 100.0F;
    const float expectedShift = kDragPixels / pixelsPerUnit;

    NativeViewport horizontal(kCameraRadius);
    horizontal.setCameraBoundsEnabled(false);
    horizontal.setSurfaceSize(kWidth, kHeight);
    horizontal.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);
    horizontal.moveByScreenDelta(kDragPixels, 0.0F);
    assertTrue(std::abs(horizontal.cameraX() - (-expectedShift)) < 0.0005F,
            "A horizontal drag must move the camera by the pixels it covered, got "
                    + std::to_string(horizontal.cameraX()));
    assertTrue(horizontal.cameraY() == 0.0F,
            "A horizontal drag must not move the camera on the other axis");

    NativeViewport vertical(kCameraRadius);
    vertical.setCameraBoundsEnabled(false);
    vertical.setSurfaceSize(kWidth, kHeight);
    vertical.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);
    vertical.moveByScreenDelta(0.0F, kDragPixels);
    assertTrue(std::abs(vertical.cameraY() - expectedShift) < 0.0005F,
            "A vertical drag must move the camera by the pixels it covered, got "
                    + std::to_string(vertical.cameraY()));
    assertTrue(vertical.cameraX() == 0.0F,
            "A vertical drag must not move the camera on the other axis");

    // Both axes keep the one scale after moving, so panning cannot distort the world.
    const std::array<float, 16> panned = projectionMatrixFor(viewport);
    assertTrue(panned[0] == matrix[0] && panned[5] == matrix[5],
            "Panning must leave the projection's scale untouched");

    // A drag that moves the camera has to move the image. This is the assertion that
    // would have caught a camera which was applied and then discarded, which is the
    // other failure this path had.
    assertTrue(panned[12] != matrix[12] && panned[13] != matrix[13],
            "A two-axis drag must move the projection's translation on both axes");

    // Every window shape must keep the world round. The normalised scales differ by
    // design on a non-square surface, so the check is again about pixels: the world
    // distance that spans the shorter side must span the same number of pixels on both
    // axes, at any shape and in either orientation.
    for (const auto& extent : {std::pair<int, int>{800, 800},
                 std::pair<int, int>{1280, 800}, std::pair<int, int>{800, 1280}}) {
        NativeViewport shaped(kCameraRadius);
        shaped.setCameraBoundsEnabled(false);
        shaped.setSurfaceSize(extent.first, extent.second);
        shaped.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);
        const std::array<float, 16> shapedMatrix = projectionMatrixFor(shaped);
        const float shapedPixelsX =
                shapedMatrix[0] * worldExtent * static_cast<float>(extent.first) * 0.5F;
        const float shapedPixelsY =
                shapedMatrix[5] * worldExtent * static_cast<float>(extent.second) * 0.5F;
        const float shapedRatio = shapedPixelsX / shapedPixelsY;
        assertTrue(std::abs(shapedRatio - 1.0F) < 0.001F,
                "A " + std::to_string(extent.first) + "x"
                        + std::to_string(extent.second)
                        + " window must keep the world round, got pixel x/y="
                        + std::to_string(shapedRatio));

        // And the camera must be able to move on both axes at every shape, which is
        // what the bounds previously prevented on whichever axis the view filled.
        NativeViewport pannedViewport = shaped;
        pannedViewport.moveByScreenDelta(kPanPixelsX, kPanPixelsY);
        assertTrue(pannedViewport.cameraX() != shaped.cameraX()
                        && pannedViewport.cameraY() != shaped.cameraY(),
                "A " + std::to_string(extent.first) + "x"
                        + std::to_string(extent.second)
                        + " window must let the camera pan on both axes");
    }

    std::cout << "camera_projection_verified=1\n";
}

} // namespace

int main(int argc, char** argv) {    try {
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

        verifyCameraProjectionKeepsWorldShape();
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
