// The Windows host: a desktop window that renders the simulation through Vulkan.
//
// This is the piece the port was missing. The backend, the platform surface seam
// and the shader pipeline were already exercised by tests, but nothing owned a
// window and drove frames, so there was no way to see the renderer work on a
// desktop.
//
// The frame content comes from VulkanDemoFrameProducer, which is the same source
// the Android worker uses for its demo path. That is deliberate: the host is not a
// second renderer, it is the same renderer with a different window system, so the
// shapes on screen are built by production code rather than by host-only drawing.
//
// Input is deliberately minimal for now. The window is resizable and closes
// normally; pointer and keyboard interaction with the microscope are the next
// piece of work and are not faked here.
//
// Asset resolution: the compiled SPIR-V and the source textures live in different
// directories, which is exactly the case the two-root asset source exists for. The
// roots are taken from the command line when given, otherwise from the environment,
// otherwise from the usual build layout relative to the executable.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// GET_X_LPARAM and GET_Y_LPARAM live in the extended coordinate helpers, which
// Windows keeps separate from windows.h to make the signed conversion explicit.
#include <windowsx.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/interaction/NativeViewport.h"
#include "host/win32/Win32HostCamera.h"
#include "app/render/NativeRenderFrameSource.h"
#include "app/render/sources/VulkanDemoFrameProducer.h"
#include "app/RenderStateSnapshots.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"
#include "render/backend/vulkan/VulkanBasicCellFrame.h"

namespace {

using cellularity::app::render_worker::sources::VulkanDemoFrameProducer;
using cellularity::render::abi::VulkanBackendAssetSource;
using cellularity::render::abi::VulkanBackendSession;

using cellularity::host::win32::kBackgroundColor;
using cellularity::host::win32::kCameraRadius;
using cellularity::host::win32::projectionMatrixFor;

// Builds the presentation the renderer consumes from the live camera.
//
// The viewport's own snapshot helper is deliberately not used: it recomputes the
// matrix from an identity surface matrix and discards the one it is given, so the
// camera would have no effect on the frame at all.
[[nodiscard]] cellularity::render::RenderPresentationState presentationFrom(
        const NativeViewport& viewport) {
    using cellularity::render::RenderPresentationState;

    RenderPresentationState presentation;
    presentation.viewport.revision = viewport.revision();
    presentation.viewport.coverageRevision = viewport.revision();
    presentation.viewport.visibleBounds =
            cellularity::app::toRenderBounds(viewport.viewBounds(0.0F));
    presentation.viewport.coverageBounds = presentation.viewport.visibleBounds;
    presentation.viewport.mvpMatrix = projectionMatrixFor(viewport);
    presentation.viewport.cameraX = viewport.cameraX();
    presentation.viewport.cameraY = viewport.cameraY();
    presentation.viewport.zoom = viewport.zoom();
    presentation.viewport.viewWidth = viewport.viewWidth();
    presentation.viewport.viewHeight = viewport.viewHeight();
    presentation.viewport.surfaceWidth = viewport.surfaceWidth();
    presentation.viewport.surfaceHeight = viewport.surfaceHeight();
    presentation.backgroundColor = kBackgroundColor;
    return presentation;
}

// The window procedure is a C callback, so the state it needs to hand back lives
// at file scope rather than being threaded through a user-data pointer.
bool gRunning = true;
int gPendingWidth = 0;
int gPendingHeight = 0;

// Input is collected from window messages and applied by the frame loop, for the
// same reason resizes are: the session and the viewport must only be touched on the
// thread that owns them, and a message dispatch is not that place.
bool gDragging = false;
int gLastPointerX = 0;
int gLastPointerY = 0;
float gPendingPanX = 0.0F;
float gPendingPanY = 0.0F;
float gPendingZoomFactor = 1.0F;
float gPendingZoomX = 0.0F;
float gPendingZoomY = 0.0F;
bool gResetCameraRequested = false;

LRESULT CALLBACK hostWindowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CLOSE:
        gRunning = false;
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        gRunning = false;
        ::PostQuitMessage(0);
        return 0;
    case WM_SIZE: {
        // The resize is recorded and acted on by the frame loop. Calling into the
        // session from here would mean touching Vulkan while Windows is inside a
        // message dispatch, and the session requires a single owning thread.
        const int width = static_cast<int>(LOWORD(lParam));
        const int height = static_cast<int>(HIWORD(lParam));
        if (width > 0 && height > 0) {
            gPendingWidth = width;
            gPendingHeight = height;
        }
        return 0;
    }
    case WM_ERASEBKGND:
        // Vulkan owns every pixel of the client area. Letting Windows paint a
        // background first would flicker on every resize.
        return 1;
    case WM_LBUTTONDOWN: {
        gDragging = true;
        gLastPointerX = static_cast<int>(LOWORD(lParam));
        gLastPointerY = static_cast<int>(HIWORD(lParam));
        ::SetCapture(window);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (gDragging) {
            gDragging = false;
            ::ReleaseCapture();
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int x = static_cast<int>(LOWORD(lParam));
        const int y = static_cast<int>(HIWORD(lParam));
        if (gDragging) {
            // Dragging moves the world with the pointer, which is what a map-like
            // view is expected to do.
            gPendingPanX += static_cast<float>(x - gLastPointerX);
            gPendingPanY += static_cast<float>(y - gLastPointerY);
        }
        gLastPointerX = x;
        gLastPointerY = y;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // The wheel delta is in the high word of wParam, in multiples of 120. The
        // cursor position arrives in screen coordinates, so it is converted to the
        // client area before it is used as a zoom anchor.
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT anchor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ::ScreenToClient(window, &anchor);
        gPendingZoomFactor *= (wheelDelta > 0) ? 1.1F : (1.0F / 1.1F);
        gPendingZoomX = static_cast<float>(anchor.x);
        gPendingZoomY = static_cast<float>(anchor.y);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            gRunning = false;
            ::DestroyWindow(window);
            return 0;
        }
        if (wParam == 'R') {
            gResetCameraRequested = true;
            return 0;
        }
        break;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

// getenv is flagged as unsafe by the MSVC runtime even though the value is only
// read. Wrapping it keeps that warning out of the build without disabling the
// warning for the whole file.
std::string environmentValue(const char* name) {
#pragma warning(push)
#pragma warning(disable : 4996)
    const char* value = std::getenv(name);
#pragma warning(pop)
    return value != nullptr ? std::string(value) : std::string();
}

// The build layout puts the compiled shaders under the build tree and the source
// textures under the source tree. Both are searched relative to the executable
// first, then relative to the working directory, so the host runs from either
// place without arguments.
std::string resolveAssetRoot(const char* environmentName,
        const std::string& executableDirectory,
        const std::string& relativeFromExecutable,
        const std::string& relativeFromWorkingDirectory) {
    const std::string fromEnvironment = environmentValue(environmentName);
    if (!fromEnvironment.empty()) {
        return fromEnvironment;
    }
    const std::filesystem::path nextToExecutable =
            std::filesystem::path(executableDirectory) / relativeFromExecutable;
    if (std::filesystem::exists(nextToExecutable)) {
        return nextToExecutable.string();
    }
    const std::filesystem::path fromWorkingDirectory =
            std::filesystem::path(relativeFromWorkingDirectory);
    if (std::filesystem::exists(fromWorkingDirectory)) {
        return fromWorkingDirectory.string();
    }
    // Report the path that was expected rather than an empty string, so a failure
    // names the directory that is missing.
    return nextToExecutable.string();
}

std::string executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = ::GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path().string();
}

void reportFailure(const char* stage, const std::string& detail) {
    std::cerr << "cellularity host: " << stage << " failed";
    if (!detail.empty()) {
        std::cerr << ": " << detail;
    }
    std::cerr << "\n";
}

// Writes the captured frame as a binary PPM. The renderer returns straight RGBA8
// and PPM needs no encoder, so a capture can be inspected without adding an image
// library to the host.
bool writePpm(const std::string& path, int width, int height,
        const std::vector<std::uint8_t>& rgba) {
    const std::size_t expected =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() < expected) {
        std::cerr << "cellularity host: capture is short: got " << rgba.size()
                  << " bytes, expected " << expected << "\n";
        return false;
    }
    FILE* file = nullptr;
    if (::fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) {
        std::cerr << "cellularity host: cannot write " << path << "\n";
        return false;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (std::size_t index = 0; index + 3 < expected; index += 4) {
        std::fwrite(&rgba[index], 1, 3, file);
    }
    std::fclose(file);
    return true;
}

// Summarises a capture so a run can be judged without opening the image. A frame
// that is uniformly one colour means the draw never reached the target, which a
// byte count alone would not reveal.
void reportCaptureStatistics(const std::vector<std::uint8_t>& rgba) {
    std::uint64_t sum[3] = {0, 0, 0};
    std::uint8_t minimum[3] = {255, 255, 255};
    std::uint8_t maximum[3] = {0, 0, 0};
    const std::size_t pixelCount = rgba.size() / 4u;
    for (std::size_t index = 0; index + 3 < rgba.size(); index += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::uint8_t value = rgba[index + channel];
            sum[channel] += value;
            minimum[channel] = value < minimum[channel] ? value : minimum[channel];
            maximum[channel] = value > maximum[channel] ? value : maximum[channel];
        }
    }
    if (pixelCount == 0) {
        std::cout << "capture: empty" << std::endl;
        return;
    }
    std::cout << "capture_pixels=" << pixelCount
              << " mean_rgb=" << (sum[0] / pixelCount) << ","
              << (sum[1] / pixelCount) << "," << (sum[2] / pixelCount)
              << " min_rgb=" << static_cast<int>(minimum[0]) << ","
              << static_cast<int>(minimum[1]) << ","
              << static_cast<int>(minimum[2])
              << " max_rgb=" << static_cast<int>(maximum[0]) << ","
              << static_cast<int>(maximum[1]) << ","
              << static_cast<int>(maximum[2]) << std::endl;
}

// Renders one frame into an off-screen target and writes it out. This is the same
// draw the interactive path performs, aimed at a readback image instead of the
// swapchain, so a capture is evidence about the renderer rather than about a
// separate screenshot path.
//
// With useCamera set, the presentation comes from a viewport after the given pan
// and zoom exactly as the interactive loop would apply them, so a capture can show
// that input moves the view without a human at the window.
int captureFrame(const std::shared_ptr<VulkanBackendSession>& session,
        const std::string& outputPath, int width, int height, bool useCamera,
        float panX, float panY, float zoomFactor) {
    using cellularity::render::OffscreenRenderTarget;
    using cellularity::render::PreparedRenderFrame;
    using cellularity::render::abi::VulkanRenderSurfaceSessionAdapter;

    try {
        VulkanRenderSurfaceSessionAdapter adapter(session);
        adapter.beginOffscreenRgba(
                OffscreenRenderTarget::SubstratePreview, width, height);

        const PreparedRenderFrame frame =
                cellularity::render::vulkan::makeVulkanBasicCellDemoFrame();

        cellularity::render::RenderPresentationState presentation;
        if (useCamera) {
            NativeViewport viewport(kCameraRadius);
            // Bounds off, matching the window path and production; see the note there.
            viewport.setCameraBoundsEnabled(false);
            viewport.setSurfaceSize(width, height);
            // The capture opens where the window opens, so a capture is evidence
            // about what the user sees rather than about a different view.
            viewport.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);
            if (panX != 0.0F || panY != 0.0F) {
                viewport.moveByScreenDelta(panX, panY);
            }
            if (zoomFactor != 1.0F) {
                // --capture-zoom is relative to the opening view, matching the way
                // the wheel multiplies the current zoom rather than setting it.
                viewport.zoomAt(zoomFactor, static_cast<float>(width) / 2.0F,
                        static_cast<float>(height) / 2.0F);
            }
            presentation = presentationFrom(viewport);
            std::cout << "camera_applied pan=" << panX << "," << panY
                      << " zoom_factor=" << zoomFactor
                      << " camera=" << viewport.cameraX() << ","
                      << viewport.cameraY() << " zoom=" << viewport.zoom()
                      << " view=" << viewport.viewWidth() << "x"
                      << viewport.viewHeight() << std::endl;
        } else {
            presentation =
                    cellularity::render::vulkan::makeVulkanBasicCellDemoPresentation(
                            width, height);
        }

        const auto passStatus =
                adapter.drawMicroscopeOffscreenPass(presentation, frame);
        if (passStatus != cellularity::render::RenderPassStatus::Completed) {
            adapter.cancelOffscreenRgba(OffscreenRenderTarget::SubstratePreview);
            reportFailure("drawMicroscopeOffscreenPass",
                    "the off-screen pass did not complete");
            return 1;
        }

        const std::vector<std::uint8_t> frameBytes = adapter.finishOffscreenRgba(
                OffscreenRenderTarget::SubstratePreview);
        reportCaptureStatistics(frameBytes);
        if (!writePpm(outputPath, width, height, frameBytes)) {
            return 1;
        }
        std::cout << "capture_written=" << outputPath << " size=" << width << "x"
                  << height << std::endl;
        return 0;
    } catch (const std::exception& failure) {
        reportFailure("frame capture", failure.what());
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string exeDirectory = executableDirectory();

    // --capture renders a single frame off-screen and writes it out, which makes
    // the renderer's output checkable without a human looking at the window.
    bool captureMode = false;
    std::string capturePath;
    int captureWidth = 1280;
    int captureHeight = 800;
    bool captureWithCamera = false;
    float capturePanX = 0.0F;
    float capturePanY = 0.0F;
    float captureZoomFactor = 1.0F;
    std::vector<std::string> positional;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--capture") {
            captureMode = true;
            if (index + 1 < argc) {
                capturePath = argv[++index];
            }
        } else if (argument == "--capture-size" && index + 2 < argc) {
            captureWidth = std::atoi(argv[++index]);
            captureHeight = std::atoi(argv[++index]);
        } else if (argument == "--capture-camera") {
            // Exercises the interactive path: the presentation comes from a
            // viewport rather than the fixed fixture.
            captureWithCamera = true;
        } else if (argument == "--capture-pan" && index + 2 < argc) {
            capturePanX = static_cast<float>(std::atof(argv[++index]));
            capturePanY = static_cast<float>(std::atof(argv[++index]));
            captureWithCamera = true;

        } else if (argument == "--capture-zoom" && index + 1 < argc) {
            captureZoomFactor = static_cast<float>(std::atof(argv[++index]));
            captureWithCamera = true;
        } else {
            positional.push_back(argument);
        }
    }
    if (capturePath.empty()) {
        capturePath = "cellularity-capture.ppm";
    }

    // The generated SPIR-V sits under the build tree; the textures stay in the
    // source tree. Two roots, searched in order.
    std::string assetRoot = positional.size() > 0
            ? positional[0]
            : resolveAssetRoot("CELLULARITY_HOST_SHADER_ROOT", exeDirectory,
                      "generated/vulkanShaders/host/assets",
                      "build/win-cmake/generated/vulkanShaders/host/assets");
    std::string assetRootSecondary = positional.size() > 1
            ? positional[1]
            : resolveAssetRoot("CELLULARITY_HOST_TEXTURE_ROOT", exeDirectory,
                      "../../native/rendering/assets", "native/rendering/assets");

    std::cout << "cellularity host\n"
              << "  shader root:  " << assetRoot << "\n"
              << "  texture root: " << assetRootSecondary << std::endl;

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &hostWindowProc;
    windowClass.hInstance = instance;
    // IDC_ARROW is an ANSI resource macro; the wide form needs the cast.
    windowClass.hCursor =
            ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"CellularityHostWindow";
    if (::RegisterClassExW(&windowClass) == 0) {
        reportFailure("RegisterClassExW",
                std::to_string(::GetLastError()));
        return 1;
    }

    constexpr int kInitialWidth = 1280;
    constexpr int kInitialHeight = 800;
    // AdjustWindowRect makes the *client* area the requested size, which is what
    // the surface extent has to match.
    RECT desired{0, 0, kInitialWidth, kInitialHeight};
    ::AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);

    const HWND window = ::CreateWindowExW(0, windowClass.lpszClassName,
            L"Cellularity", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            desired.right - desired.left, desired.bottom - desired.top, nullptr,
            nullptr, instance, nullptr);
    if (window == nullptr) {
        reportFailure("CreateWindowExW", std::to_string(::GetLastError()));
        return 1;
    }
    ::ShowWindow(window, SW_SHOW);
    ::UpdateWindow(window);

    // The backend instance is a separate library, so it is obtained through the
    // same v1 entry point a client uses rather than by linking its internals.
    const CellularityVulkanSurfaceApiV1* backendApi =
            cellularity_vulkan_get_surface_api_v1();
    if (backendApi == nullptr) {
        reportFailure("cellularity_vulkan_get_surface_api_v1",
                "the backend exposed no v1 API table");
        return 1;
    }

    std::shared_ptr<VulkanBackendSession> session;
    try {
        session = VulkanBackendSession::create(
                backendApi, VulkanBackendAssetSource::fromFilesystem(
                                    assetRoot, assetRootSecondary));
    } catch (const std::exception& failure) {
        reportFailure("session creation", failure.what());
        return 1;
    }
    if (session == nullptr) {
        reportFailure("session creation", "the backend returned no session");
        return 1;
    }

    if (session->attachWindow(window, kInitialWidth, kInitialHeight)
            != CELLULARITY_VULKAN_OPERATION_OK_V1) {
        reportFailure("attachWindow",
                "the backend could not attach the window; see its diagnostics");
        return 1;
    }

    if (captureMode) {
        // The window is a real one, so the session has a device and a swapchain,
        // but nothing is ever shown because the frame is drawn off-screen.
        const int result = captureFrame(session, capturePath, captureWidth,
                captureHeight, captureWithCamera, capturePanX, capturePanY,
                captureZoomFactor);
        session->detachSurface();
        try {
            session->closeOnOwnerThread();
        } catch (const std::exception& failure) {
            reportFailure("closeOnOwnerThread", failure.what());
        }
        return result;
    }

    VulkanDemoFrameProducer producer;
    if (producer.bindBackend(session)
            != cellularity::app::render_worker::SourceSurfaceResult::Applied) {
        reportFailure("bindBackend", "the frame producer did not bind");
        return 1;
    }
    if (producer.onSurfaceAttached(1, kInitialWidth, kInitialHeight)
            != cellularity::app::render_worker::SourceSurfaceResult::Applied) {
        reportFailure("onSurfaceAttached", "the frame producer refused the extent");
        return 1;
    }

    int surfaceWidth = kInitialWidth;
    int surfaceHeight = kInitialHeight;
    std::uint64_t presented = 0;
    std::uint64_t failedFrames = 0;

    // The camera is the production viewport, so panning and zoom anchoring behave
    // exactly as they do on Android rather than being reimplemented here.
    //
    // Camera bounds are turned off explicitly. The viewport enables them by default and
    // production turns them off in its own session viewport; leaving the default in
    // place pins the camera along whichever axis the opening view exactly fills, which
    // in a landscape window is the vertical one, so a vertical drag would do nothing at
    // all while a horizontal one moved.
    NativeViewport viewport(kCameraRadius);
    viewport.setCameraBoundsEnabled(false);
    viewport.setSurfaceSize(surfaceWidth, surfaceHeight);
    viewport.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);

    while (gRunning) {
        // Pump without blocking so a frame is still drawn when the user is not
        // interacting with the window.
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) {
                gRunning = false;
                break;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (!gRunning) {
            break;
        }

        if (gPendingWidth > 0 && gPendingHeight > 0
                && (gPendingWidth != surfaceWidth
                        || gPendingHeight != surfaceHeight)) {
            surfaceWidth = gPendingWidth;
            surfaceHeight = gPendingHeight;
            gPendingWidth = 0;
            gPendingHeight = 0;
            if (session->resizeSurface(surfaceWidth, surfaceHeight)
                    != CELLULARITY_VULKAN_OPERATION_OK_V1) {
                // A resize can legitimately fail while the window is minimized or
                // mid-drag; the next frame retries with whatever extent arrives.
                std::cerr << "cellularity host: surface resize to " << surfaceWidth
                          << "x" << surfaceHeight << " was refused\n";
            }
            if (producer.onSurfaceResized(surfaceWidth, surfaceHeight)
                    != cellularity::app::render_worker::SourceSurfaceResult::Applied) {
                std::cerr << "cellularity host: frame producer refused the resize\n";
            }
            viewport.setSurfaceSize(surfaceWidth, surfaceHeight);
        }

        // Input collected from window messages is applied here, on the thread that
        // owns the viewport and the session.
        if (gResetCameraRequested) {
            gResetCameraRequested = false;
            viewport.setCamera(0.0F, 0.0F, 1.0F / kCameraRadius);
        }
        if (gPendingPanX != 0.0F || gPendingPanY != 0.0F) {
            viewport.moveByScreenDelta(gPendingPanX, gPendingPanY);
            gPendingPanX = 0.0F;
            gPendingPanY = 0.0F;
        }
        if (gPendingZoomFactor != 1.0F) {
            viewport.zoomAt(gPendingZoomFactor, gPendingZoomX, gPendingZoomY);
            gPendingZoomFactor = 1.0F;
        }

        // The presentation is rebuilt from the live camera every frame, so a drag or
        // a wheel event shows up in the next frame without any other plumbing.
        producer.setPresentationOverride(presentationFrom(viewport));

        const auto frame = producer.renderFrame();
        if (frame.status
                == cellularity::app::render_worker::SourceFrameStatus::Failed) {
            ++failedFrames;
            std::cerr << "cellularity host: frame " << presented
                      << " failed to render\n";
            if (failedFrames >= 3) {
                reportFailure("render loop",
                        "three consecutive frames failed; stopping so the cause is "
                        "visible instead of repeating");
                break;
            }
        } else {
            ++presented;
            failedFrames = 0;
            // Report progress periodically. The output is flushed explicitly
            // because a redirected stream is block-buffered, and a host that is
            // running but silent is indistinguishable from one that is stuck.
            if (presented % 120 == 0) {
                std::cout << "cellularity host: presented " << presented
                          << " frames at " << surfaceWidth << "x" << surfaceHeight
                          << std::endl;
            }
        }

        // The session presents as part of the frame call, so there is nothing to
        // swap here. A short sleep keeps the loop from spinning a core when the
        // display is the throttle, which a blocking present would do on its own.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "cellularity host: presented " << presented << " frames\n";

    // Shutdown order matters. The frame producer releases its reference to the
    // session first, then the surface is detached, and only then is the session
    // closed. closeOnOwnerThread is the explicit teardown: the destructor refuses
    // to release backend resources because it cannot prove it is running on the
    // thread that owns them, and leaks them instead. Calling it here is what makes
    // the host exit cleanly.
    producer.onSurfaceDetached(
            cellularity::app::render_worker::SurfaceDetachReason::Ordinary);
    producer.unbindBackend();
    session->detachSurface();
    try {
        session->closeOnOwnerThread();
    } catch (const std::exception& failure) {
        reportFailure("closeOnOwnerThread", failure.what());
    }
    // The session is released before the window it borrowed, because the backend
    // presents into that window.
    session.reset();
    return 0;
}
