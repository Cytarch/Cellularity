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

#include <algorithm>
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

#include "app/NativeCellularityController.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "host/win32/Win32HostCamera.h"
#include "app/RenderStateSnapshots.h"
#include "core/Environment.h"
#include "formats/DecodedWorldValidation.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"
#include "render/frame/PreparedRenderFrame.h"

namespace {

using cellularity::render::abi::VulkanBackendAssetSource;
using cellularity::render::abi::VulkanBackendSession;

using cellularity::host::win32::kBackgroundColor;
using cellularity::host::win32::kCameraRadius;

// The same configuration the Android app opens a substrate with, so a desktop run
// and a phone run are the same simulation rather than two similar ones.
constexpr int kMaxCellCount = 1000;
constexpr int kMaxFoodCount = 3500;
constexpr double kDishRadius = 1.0;

// How long a capture waits for the simulation to produce a drawable frame. At the
// step rate below this is several seconds of simulated world, which is far more than
// a fresh substrate needs to show cells.
constexpr int kMaximumCaptureAttempts = 900;

// The window procedure is a C callback, so the state it needs to hand back lives
// at file scope rather than being threaded through a user-data pointer.
bool gRunning = true;
int gPendingWidth = 0;
int gPendingHeight = 0;

// Input is collected from window messages and applied by the frame loop, for the
// same reason resizes are: the session and the simulation must only be touched on
// the thread that owns them, and a message dispatch is not that place.
//
// Screen coordinates here are client-area pixels with the origin at the top left,
// which is the space the simulation expects. It converts them to world coordinates
// through its own viewport at the moment the event is queued, so the extent the host
// reports to the controller has to be the client area or taps land in the wrong place.
bool gDragging = false;
int gLastPointerX = 0;
int gLastPointerY = 0;
bool gPointerMoved = false;
float gPendingPanX = 0.0F;
float gPendingPanY = 0.0F;
float gPendingZoomFactor = 1.0F;
float gPendingZoomX = 0.0F;
float gPendingZoomY = 0.0F;
bool gResetCameraRequested = false;
float gPendingTapX = 0.0F;
float gPendingTapY = 0.0F;
bool gTapRequested = false;
int gPendingTemperature = -1;
int gPendingTool = -1;

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
        gPointerMoved = false;
        gLastPointerX = static_cast<int>(LOWORD(lParam));
        gLastPointerY = static_cast<int>(HIWORD(lParam));
        ::SetCapture(window);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (gDragging) {
            gDragging = false;
            ::ReleaseCapture();
            // A press that never turned into a drag is a click, and a click is the
            // gesture that selects a cell or places one. Recording it here rather
            // than dispatching it keeps the simulation off the message thread.
            if (!gPointerMoved) {
                gPendingTapX = static_cast<float>(LOWORD(lParam));
                gPendingTapY = static_cast<float>(HIWORD(lParam));
                gTapRequested = true;
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int x = static_cast<int>(LOWORD(lParam));
        const int y = static_cast<int>(HIWORD(lParam));
        if (gDragging) {
            // Dragging moves the world with the pointer, which is what a map-like
            // view is expected to do.
            const float deltaX = static_cast<float>(x - gLastPointerX);
            const float deltaY = static_cast<float>(y - gLastPointerY);
            if (deltaX != 0.0F || deltaY != 0.0F) {
                gPointerMoved = true;
            }
            gPendingPanX += deltaX;
            gPendingPanY += deltaY;
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
        // The simulation's own controls, so the desktop host can drive every mode
        // the touch UI can rather than only the camera.
        if (wParam >= '1' && wParam <= '5') {
            gPendingTemperature = static_cast<int>(wParam - '1');
            return 0;
        }
        if (wParam >= '6' && wParam <= '9') {
            gPendingTool = static_cast<int>(wParam - '6');
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

} // namespace

namespace cellularity::render {

// A surface session that draws the simulation's presentation into an off-screen
// target instead of the swapchain.
//
// The simulation decides when to draw and hands its own presentation to the session
// it was given, so the only way to aim a live frame at a readback image is to be that
// session. Everything except the microscope draw is forwarded unchanged, which keeps
// the off-screen target the single thing this differs by.
//
// The frame cannot be stored for a later draw: PreparedRenderFrame is move-only, and
// the reference it arrives by is only valid for the call. So the off-screen draw
// happens inside presentMicroscope, on the frame that is live at that moment, and
// the host arms the capture for the one step it wants a still of.
class CaptureSurfaceSession final : public RenderSurfaceSession {
public:
    CaptureSurfaceSession(std::shared_ptr<VulkanBackendSession> session, int width,
            int height)
        : mSession(std::move(session)), mAdapter(mSession), mWidth(width),
          mHeight(height) {
    }

    // Arms the capture and starts the off-screen transaction. Called once, after the
    // warm-up, so the still is of the world at the end of the warm-up rather than of
    // whichever step happened to be drawn first.
    void armCapture() {
        mCaptureArmed = true;
        mAdapter.beginOffscreenRgba(OffscreenRenderTarget::SubstratePreview, mWidth,
                mHeight);
    }

    // True once the host has armed the capture and the target has been drawn into.
    [[nodiscard]] bool captureDrawn() const noexcept {
        return mCaptureArmed;
    }

    [[nodiscard]] std::uint64_t presentedCalls() const noexcept {
        return mPresentedCalls;
    }

    [[nodiscard]] std::uint64_t clearedCalls() const noexcept {
        return mClearedCalls;
    }

    [[nodiscard]] RenderPassStatus capturedPassStatus() const noexcept {
        return mCapturedPassStatus;
    }

    void cancel() noexcept {
        mAdapter.cancelOffscreenRgba(OffscreenRenderTarget::SubstratePreview);
    }

    [[nodiscard]] std::vector<std::uint8_t> finish() {
        return mAdapter.finishOffscreenRgba(OffscreenRenderTarget::SubstratePreview);
    }

    [[nodiscard]] RenderBackendKind kind() const noexcept override {
        return mAdapter.kind();
    }

    [[nodiscard]] const void* bindingIdentity() const noexcept override {
        return mAdapter.bindingIdentity();
    }

    [[nodiscard]] bool hasResources() const noexcept override {
        return mAdapter.hasResources();
    }

    [[nodiscard]] bool hasGpuBuffers() const noexcept override {
        return mAdapter.hasGpuBuffers();
    }

    void attachSurface(bool previewMode, const NativeRenderAssets& assets,
            NativeRenderTextureLoader& textureLoader) override {
        mAdapter.attachSurface(previewMode, assets, textureLoader);
    }

    void resizeSurface(int width, int height) override {
        mAdapter.resizeSurface(width, height);
    }

    void detachSurface() override {
        mAdapter.detachSurface();
    }

    void abandonSurface() noexcept override {
        mAdapter.abandonSurface();
    }

    [[nodiscard]] RenderFrameResult presentMicroscope(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry) override {
        (void)debugHudTelemetry;
        ++mPresentedCalls;
        mCapturedPassStatus =
                mAdapter.drawMicroscopeOffscreenPass(presentation, frame);
        // The capture redirects the draw rather than replacing it, so the frame really
        // was drawn -- it went to the readback image instead of a window. That makes
        // this a genuine primary presentation, which is what the caller validates: a
        // Presented outcome has to carry exactly that flag and no other.
        return makeRenderFrameResult(
                RenderFrameOutcome::Presented, kPrimaryContentPresented);
    }

    [[nodiscard]] RenderPassStatus drawMicroscopeOffscreenPass(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame) override {
        return mAdapter.drawMicroscopeOffscreenPass(presentation, frame);
    }

    [[nodiscard]] RenderFrameResult clearSurface(
            const RenderViewportState& viewport,
            const std::array<float, 4>& color) override {
        ++mClearedCalls;
        return mAdapter.clearSurface(viewport, color);
    }

    [[nodiscard]] RenderFrameResult drawEnvironmentFieldPreview(
            const EnvironmentFieldPreviewCommand& command) override {
        return mAdapter.drawEnvironmentFieldPreview(command);
    }

    void beginGenomePreviewFrame(int width, int height,
            bool fullyTransparentClear) override {
        mAdapter.beginGenomePreviewFrame(width, height, fullyTransparentClear);
    }

    void drawGenomePreviewGrid(const GenomePreviewGridCommand& command) override {
        mAdapter.drawGenomePreviewGrid(command);
    }

    void drawGenomePreviewTree(const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame, const std::array<float, 16>& matrix,
            const std::vector<float>& lineVertices,
            const GenomePreviewDrawOptions& options) override {
        mAdapter.drawGenomePreviewTree(
                presentation, frame, matrix, lineVertices, options);
    }

    void drawGenomePreview(const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const GenomePreviewDrawOptions& options) override {
        mAdapter.drawGenomePreview(presentation, frame, options);
    }

    [[nodiscard]] RenderFrameResult endGenomePreviewFrame() override {
        return mAdapter.endGenomePreviewFrame();
    }

    [[nodiscard]] RenderPassStatus endGenomePreviewOffscreenPass() override {
        return mAdapter.endGenomePreviewOffscreenPass();
    }

    void cancelGenomePreviewFrame() noexcept override {
        mAdapter.cancelGenomePreviewFrame();
    }

    void beginOffscreenRgba(OffscreenRenderTarget target, int width,
            int height) override {
        mAdapter.beginOffscreenRgba(target, width, height);
    }

    [[nodiscard]] std::vector<std::uint8_t> finishOffscreenRgba(
            OffscreenRenderTarget target) override {
        return mAdapter.finishOffscreenRgba(target);
    }

    void cancelOffscreenRgba(OffscreenRenderTarget target) noexcept override {
        mAdapter.cancelOffscreenRgba(target);
    }

private:
    std::shared_ptr<VulkanBackendSession> mSession;
    abi::VulkanRenderSurfaceSessionAdapter mAdapter;
    int mWidth = 0;
    int mHeight = 0;
    std::uint64_t mPresentedCalls = 0;
    std::uint64_t mClearedCalls = 0;
    bool mCaptureArmed = false;
    RenderPassStatus mCapturedPassStatus = RenderPassStatus::Failed;
};

} // namespace cellularity::render

namespace {

// Renders one frame of the live simulation into an off-screen target and writes it
// out. This is the same draw the interactive path performs, aimed at a readback image
// instead of the swapchain, so a capture is evidence about the renderer and the
// simulation rather than about a separate screenshot path.
//
// The runtime drives the world exactly as it does in the window, because the
// alternative -- stepping the world by hand from this thread -- is a different
// execution path that does not advance the world at all in the renderable stages,
// which makes it useless as evidence about the window. So capture starts the runtime,
// waits for the simulation to publish a frame, and reads that frame back.
int captureFrame(const std::shared_ptr<VulkanBackendSession>& session,
        const std::string& outputPath, int width, int height, bool useCamera,
        float panX, float panY, float zoomFactor, int temperatureMode,
        int warmupFrames) {
    using cellularity::render::CaptureSurfaceSession;

    try {
        Environment environment = Environment::constructDefaultEnvironment(
                kMaxCellCount, kMaxFoodCount, kDishRadius);
        // A fresh world is empty of cells unless the environment seeds them, so a
        // capture of a run that has not been played yet would show food and nothing
        // else.
        environment.addRandom = true;
        cellularity::snapshot_codec::validateEnvironmentForAllocation(environment);

        auto controller = std::make_shared<NativeCellularityController>(
                false, environment);
        {
            auto access = controller->writeWorld();
            access.world().mCreatedAtUnixMillis =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
            cellularity::snapshot_codec::preserveEnvironmentRuntimeFields(
                    access.world(), environment);
        }

        auto captureSession =
                std::make_unique<CaptureSurfaceSession>(session, width, height);
        CaptureSurfaceSession* capture = captureSession.get();
        // Every draw goes into the off-screen target, so the target has to be open
        // before the runtime starts publishing frames.
        capture->armCapture();
        controller->installRenderSurfaceSession(std::move(captureSession));
        controller->setViewportSurfaceSize(width, height);
        // Defaults to the opening view the interactive host shows, so a capture is
        // evidence about what the user sees rather than about a different view.
        controller->setViewportCamera(0.0F, 0.0F, 1.0F / kDishRadius);
        controller->setMicroscopeTemperatureMode(temperatureMode);

        if (useCamera) {
            if (panX != 0.0F || panY != 0.0F) {
                controller->moveViewportByScreenDelta(panX, panY);
            }
            if (zoomFactor != 1.0F) {
                // --capture-zoom is relative to the opening view, matching the way
                // the wheel multiplies the current zoom rather than setting it.
                controller->zoomViewportAt(zoomFactor,
                        static_cast<float>(width) / 2.0F,
                        static_cast<float>(height) / 2.0F);
            }
            const std::array<float, 7> state = controller->viewportState();
            std::cout << "camera_applied pan=" << panX << "," << panY
                      << " zoom_factor=" << zoomFactor
                      << " camera=" << state[0] << "," << state[1]
                      << " zoom=" << state[2] << " view=" << state[3] << "x"
                      << state[4] << " scale=" << state[5] << std::endl;
        }

        controller->startMicroscopeRuntime();

        // Draw until the simulation has published a frame, then keep going for the
        // warm-up so the still is of a world that has run rather than of the empty
        // one it started as.
        const int totalDraws = std::max(1, warmupFrames);
        for (int draw = 0; draw < totalDraws; ++draw) {
            controller->drawMicroscopeForCurrentMode();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        // A fresh world has nothing to show until cells have appeared, so if no frame
        // has been published yet, keep drawing rather than reporting success on an
        // image of the background.
        int captureAttempts = 0;
        while (captureAttempts < kMaximumCaptureAttempts
                && capture->presentedCalls() == 0) {
            controller->drawMicroscopeForCurrentMode();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            ++captureAttempts;
        }
        if (capture->presentedCalls() == 0) {
            const auto stalled = controller->viewportState();
            std::cerr << "cellularity host: capture stalled after " << captureAttempts
                      << " draws (view=" << stalled[3] << "x" << stalled[4]
                      << " scale=" << stalled[5]
                      << " clears=" << capture->clearedCalls() << ")" << std::endl;
        }

        if (capture->presentedCalls() == 0
                || capture->capturedPassStatus()
                        != cellularity::render::RenderPassStatus::Completed) {
            capture->cancel();
            reportFailure("simulation capture",
                    "the simulation never drew a frame into the off-screen target, "
                    "so there was nothing to read back");
            session->detachSurface();
            return 1;
        }

        const auto snapshot = controller->microscopeRuntimeSnapshot();
        std::cout << "simulation_world cells=" << snapshot.cellCount
                  << " updates=" << snapshot.completedUpdateCount
                  << " simulated_time=" << snapshot.simulationTime << std::endl;

        const std::vector<std::uint8_t> frameBytes = capture->finish();
        reportCaptureStatistics(frameBytes);
        if (!writePpm(outputPath, width, height, frameBytes)) {
            session->detachSurface();
            return 1;
        }
        std::cout << "capture_written=" << outputPath << " size=" << width << "x"
                  << height << std::endl;
        session->detachSurface();
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
    int captureTemperature = 1;
    int captureWarmup = 90;
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
            // Exercises the interactive path: the presentation comes from the
            // simulation's own viewport rather than a fixed opening view.
            captureWithCamera = true;
        } else if (argument == "--capture-pan" && index + 2 < argc) {
            capturePanX = static_cast<float>(std::atof(argv[++index]));
            capturePanY = static_cast<float>(std::atof(argv[++index]));
            captureWithCamera = true;

        } else if (argument == "--capture-zoom" && index + 1 < argc) {
            captureZoomFactor = static_cast<float>(std::atof(argv[++index]));
            captureWithCamera = true;
        } else if (argument == "--capture-temperature" && index + 1 < argc) {
            captureTemperature = std::atoi(argv[++index]);
        } else if (argument == "--capture-warmup" && index + 1 < argc) {
            captureWarmup = std::atoi(argv[++index]);
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
                captureZoomFactor, captureTemperature, captureWarmup);
        session->detachSurface();
        try {
            session->closeOnOwnerThread();
        } catch (const std::exception& failure) {
            reportFailure("closeOnOwnerThread", failure.what());
        }
        return result;
    }

    // The live simulation. The world, the camera and the frame cadence all belong to
    // the controller, so the host neither steps the world nor builds a presentation:
    // it owns the window, the surface and the input, and draws whatever the
    // simulation has ready.
    std::shared_ptr<NativeCellularityController> controller;
    try {
        Environment environment = Environment::constructDefaultEnvironment(
                kMaxCellCount, kMaxFoodCount, kDishRadius);
        // A fresh substrate with no cells is a valid world but a poor first
        // impression, and the app's own "new substrate" path seeds life the same way.
        environment.addRandom = true;
        cellularity::snapshot_codec::validateEnvironmentForAllocation(environment);

        controller =
                std::make_shared<NativeCellularityController>(
                        false, environment);
        {
            auto access = controller->writeWorld();
            access.world().mCreatedAtUnixMillis =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
            cellularity::snapshot_codec::preserveEnvironmentRuntimeFields(
                    access.world(), environment);
        }
        controller->installRenderSurfaceSession(
                std::make_unique<
                        cellularity::render::abi::VulkanRenderSurfaceSessionAdapter>(
                        session));
        controller->setViewportSurfaceSize(kInitialWidth, kInitialHeight);
        // Observe is the mode that runs the world at a watchable speed.
        controller->setMicroscopeTemperatureMode(1);
        controller->startMicroscopeRuntime();
    } catch (const std::exception& failure) {
        reportFailure("simulation startup", failure.what());
        return 1;
    }

    int surfaceWidth = kInitialWidth;
    int surfaceHeight = kInitialHeight;
    std::uint64_t presented = 0;
    std::uint64_t skipped = 0;
    std::uint64_t failedFrames = 0;

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
            // The extent the simulation projects into and converts taps through has
            // to be the client area, or input lands somewhere other than the cursor.
            controller->setViewportSurfaceSize(surfaceWidth, surfaceHeight);
            controller->invalidateMicroscopeFrameData();
        }

        // Input collected from window messages is applied here, on the thread that
        // owns the simulation. These calls only enqueue: the simulation resolves
        // them against its own camera at the moment it can safely apply them.
        if (gResetCameraRequested) {
            gResetCameraRequested = false;
            // A reset returns the view as well as the world, which is what the
            // key promises.
            controller->resetMicroscopeWorld();
        }
        if (gPendingPanX != 0.0F || gPendingPanY != 0.0F) {
            controller->queueMicroscopePan(gPendingPanX, gPendingPanY);
            gPendingPanX = 0.0F;
            gPendingPanY = 0.0F;
        }
        if (gPendingZoomFactor != 1.0F) {
            controller->queueMicroscopeZoom(
                    gPendingZoomFactor, gPendingZoomX, gPendingZoomY);
            gPendingZoomFactor = 1.0F;
        }
        if (gTapRequested) {
            gTapRequested = false;
            // A tap with no genome selects or deselects whatever is under the
            // cursor, which is the whole of what clicking a plate does.
            controller->queueMicroscopeTap(gPendingTapX, gPendingTapY, nullptr);
        }
        if (gPendingTemperature >= 0) {
            controller->setMicroscopeTemperatureMode(gPendingTemperature);
            gPendingTemperature = -1;
        }
        if (gPendingTool >= 0) {
            controller->setMicroscopeToolOrdinal(gPendingTool);
            gPendingTool = -1;
        }

        // A minimized window has no client area, so there is nothing to present to and
        // no valid extent to project into. Skipping the frame keeps the loop alive and
        // keeps the console free of a resize complaint per iteration; the window sends
        // another size message when it is restored.
        if (surfaceWidth <= 0 || surfaceHeight <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // The simulation owns the world on its own thread, so the host never steps
        // it; it draws the newest frame the simulation has captured.
        const auto frame = controller->drawMicroscopeForCurrentMode();
        using cellularity::render::RenderFrameOutcome;
        if (frame.outcome == RenderFrameOutcome::Failed
                || frame.outcome == RenderFrameOutcome::SurfaceLost) {
            ++failedFrames;
            std::cerr << "cellularity host: frame " << presented
                      << " failed to render (outcome "
                      << static_cast<int>(frame.outcome) << ")\n";
            if (failedFrames >= 3) {
                reportFailure("render loop",
                        "three consecutive frames failed; stopping so the cause is "
                        "visible instead of repeating");
                break;
            }
        } else if (frame.outcome == RenderFrameOutcome::Presented) {
            ++presented;
            failedFrames = 0;
            // Report progress periodically. The output is flushed explicitly
            // because a redirected stream is block-buffered, and a host that is
            // running but silent is indistinguishable from one that is stuck.
            if (presented % 300 == 0) {
                const auto snapshot = controller->microscopeRuntimeSnapshot();
                std::cout << "cellularity host: presented " << presented
                          << " frames at " << surfaceWidth << "x" << surfaceHeight
                          << " cells=" << snapshot.cellCount
                          << " updates=" << snapshot.completedUpdateCount
                          << " simulated=" << snapshot.simulationTime << std::endl;
            }
        } else {
            // Deferred and SkippedNotReady are the normal state while the simulation
            // is between captures, so they are counted but never treated as faults.
            ++skipped;
        }

        // The session presents as part of the frame call, so there is nothing to
        // swap here. A short sleep keeps the loop from spinning a core when the
        // display is the throttle, which a blocking present would do on its own.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "cellularity host: presented " << presented << " frames, skipped "
              << skipped << std::endl;

    // Shutdown order matters. The simulation is stopped first, because it owns a
    // thread that writes the world the renderer reads. Then the surface is detached
    // and only then is the session closed. closeOnOwnerThread is the explicit
    // teardown: the destructor refuses to release backend resources because it
    // cannot prove it is running on the thread that owns them, and leaks them
    // instead. Calling it here is what makes the host exit cleanly.
    try {
        controller->stopMicroscopeRuntime();
    } catch (const std::exception& failure) {
        reportFailure("stopMicroscopeRuntime", failure.what());
    }
    controller.reset();
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
