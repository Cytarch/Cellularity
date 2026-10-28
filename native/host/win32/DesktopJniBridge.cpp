// The Windows JNI bridge: the desktop simulation, in a window the JVM owns.
//
// Win32Host.cpp is a process. It creates a top-level window, owns a message loop and
// drives frames until the user closes it. A managed desktop application cannot use
// that shape, because it already has a window and an event loop of its own. This
// translation unit is the same simulation behind the other front door: it creates a
// *child* window of an HWND the JVM passes in, stands the same Vulkan session up on
// it, and exposes the handful of operations a frame loop and an input path need as
// JNI entry points. It links the same libraries the standalone host links, so the
// two front doors run one renderer rather than two similar ones.
//
// ## Symbol names
//
// The JVM binds an `external fun` to a C++ symbol named
// `Java_<package>_<Class>_<method>` with the dots of the qualified class name
// replaced by underscores, and every literal underscore in the package or class name
// escaped as `_1`. The class this file serves is
// com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer, whose
// segments are com, cytarch, cellularity, desktop, nativebridge and
// DesktopNativeRenderer. None of them contains an underscore, so the escaping rule
// has nothing to escape and the prefix below is the plain translation of the dotted
// name. Renaming the package or the class without renaming these symbols is not a
// load-time failure: the library still loads and the mismatch surfaces as an
// UnsatisfiedLinkError on the first call.
//
// The start entry point is exported under two names, nativeStartSession and
// nativeCreateSession, because the managed declaration and the specification for this
// bridge disagree about which one it is. Both forward to one implementation; see the
// alias below.
//
// ## Threading: one calling thread, and it is not the simulation's
//
// VulkanBackendSession records the std::thread::id that created it, and every
// operation it forwards refuses to run on any other thread (see its mOwnerThread and
// beginCall). The swapchain, the command buffers and the controller's render-facing
// state are single-owner for the same reason. The managed side is therefore required
// to call nativeStartSession, nativeRenderFrame and nativeStopSession from one and
// the same thread for the lifetime of a session. Nothing here makes that requirement
// optional, and a call from a second thread fails rather than working by accident.
//
// That thread does not simulate anything. startMicroscopeRuntime() starts the
// runtime's own owner thread, which steps the world, resolves queued input against
// its own camera and publishes captured frames; the calling thread only presents the
// newest published frame. The input entry points below therefore never touch the
// world: they enqueue a command the runtime applies at a point it can prove is safe.
// That is the same division of labour the standalone host has, where the window
// procedure records input and the frame loop submits it.
//
// ## Failure
//
// No C++ exception may cross the JNI boundary, so every entry point here catches
// everything and reports through its return value. The codes are documented per
// function. Two conventions are shared: 0 means success or "nothing to do" for the
// status-returning calls, and a handle this bridge does not recognise is never
// dereferenced. Handles are validated against a registry rather than trusted, which
// is what makes a stale or double-stopped handle a refusal instead of a
// use-after-free.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <jni.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "app/NativeCellularityController.h"
// microscopeRuntimeSnapshot() returns this by value, so the controller's forward
// declaration is not enough to read the cell count out of it.
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "core/Environment.h"
#include "core/SimulationWorld.h"
#include "formats/DecodedWorldValidation.h"
#include "render/backend/abi/CellularityVulkanBackendAbi.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"

namespace {

using cellularity::render::RenderFrameOutcome;
using cellularity::render::abi::VulkanBackendAssetSource;
using cellularity::render::abi::VulkanBackendSession;

// Status codes. They are values rather than exceptions because the managed
// declarations are plain `Int` returns: the callers are a frame loop and an input
// path, both of which want to keep going and neither of which has anything useful to
// do with a stack trace.
constexpr jint kStatusOk = 0;
constexpr jint kStatusFailed = 1;
// Deliberately distinct from kStatusFailed: "your handle is not one of mine" is a
// caller bug, while a failed resize or teardown is an environment problem, and a
// caller that logs both the same way learns nothing.
constexpr jint kStatusInvalidHandle = 2;

// Input is queued or it is not. The managed declarations return Int rather than
// Boolean, so the values are 1 and 0 rather than JNI_TRUE and JNI_FALSE.
constexpr jint kInputQueued = 1;
constexpr jint kInputRefused = 0;

// The window class this bridge registers. It belongs to this DLL rather than to the
// executable, which matters: the backend resolves the HINSTANCE that owns a window
// class from the window itself when it creates the Vulkan surface, and a class
// registered against the wrong module produces a surface that is created
// successfully but that no device reports as presentable.
constexpr const wchar_t* kChildWindowClassName =
        L"CellularityDesktopJniChildWindow";

// The HINSTANCE of this DLL. LoadLibrary does not hand it to the code it loads, so it
// is recovered from the address of a function that is provably inside it. The
// fallback is the executable's module, which is what GetModuleHandleW(nullptr)
// returns and what a statically linked build would have to use anyway.
HINSTANCE moduleInstance() noexcept {
    static const HINSTANCE instance = []() -> HINSTANCE {
        HINSTANCE module = nullptr;
        if (::GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&moduleInstance), &module)
                == 0) {
            return ::GetModuleHandleW(nullptr);
        }
        return module;
    }();
    return instance;
}

LRESULT CALLBACK childWindowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);

// One window class for every child window this bridge creates. Registering it once
// is not an optimisation: RegisterClassExW fails with ERROR_CLASS_ALREADY_EXISTS for
// a second attempt in the same process, and that has to be told apart from a real
// failure rather than reported as one.
bool ensureChildWindowClassRegistered() noexcept {
    static const bool registered = []() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &childWindowProc;
        windowClass.hInstance = moduleInstance();
        // IDC_ARROW is the ANSI spelling of a resource identifier; the wide form is
        // what a WNDCLASSEXW takes.
        windowClass.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kChildWindowClassName;
        if (::RegisterClassExW(&windowClass) != 0) {
            return true;
        }
        return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}

// The child window never paints and never closes itself. Vulkan owns every pixel of
// its client area, so letting Windows erase a background first would put a flash of
// class brush under the presented frame on every resize; and the window is a child of
// a window the JVM owns, so its lifetime is the managed side's decision, expressed by
// nativeStopSession and nativeDestroyWindow rather than by a message.
LRESULT CALLBACK childWindowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

// A session is a heap-allocated struct, held here rather than by an address the
// managed side owns. The handle handed out is a monotonic key into this registry, not
// the address of the struct: an address can be handed back out by the allocator after
// the session is destroyed, so a stale handle would silently name a different live
// session, while a key is never reissued within a process lifetime.
struct DesktopSession final {
    // The child window this session presents into. It is written once, before the
    // session is published to the registry, and never changed afterwards, so it can
    // be read without the lock below.
    HWND window = nullptr;

    // The backend session is the same object the standalone host creates. It is what
    // owns the device and the swapchain, and it is the object that must be closed on
    // the thread that created it.
    std::shared_ptr<VulkanBackendSession> session;

    // The composition root: the world, the camera, the input queues and the runtime
    // that steps them. Shared rather than unique for the same reason the host shares
    // it: the render surface session adapter is installed into it and holds the
    // backend, so both have to outlive the controller's own teardown.
    std::shared_ptr<NativeCellularityController> controller;

    // Guards the members above against a stop that races a call already in flight. It
    // does not serialise the backend or the simulation, which are single-threaded by
    // construction; it only keeps a call that has already started from reading a
    // member another thread is clearing.
    std::mutex mutex;
};

// One lock covers both registries. They are touched once per session and once per
// window rather than once per frame, so a finer split would buy nothing and cost the
// reader a second ordering to reason about.
std::mutex gStateMutex;
std::unordered_set<HWND> gChildWindows;
std::unordered_map<jlong, std::shared_ptr<DesktopSession>> gSessions;
jlong gNextSessionHandle = 1;

[[nodiscard]] HWND windowFromHandle(jlong windowHandle) noexcept {
    return reinterpret_cast<HWND>(static_cast<std::intptr_t>(windowHandle));
}

[[nodiscard]] jlong handleFromWindow(HWND window) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(window));
}

void registerChildWindow(HWND window) {
    std::lock_guard<std::mutex> lock(gStateMutex);
    gChildWindows.insert(window);
}

// True only for the windows this bridge created, so a handle the managed side made up
// -- or one from a window that has already been destroyed -- is refused instead of
// being passed to a Win32 call that would act on whatever now owns the address.
[[nodiscard]] bool isBridgeWindow(HWND window) {
    if (window == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(gStateMutex);
    return gChildWindows.find(window) != gChildWindows.end();
}

[[nodiscard]] std::shared_ptr<DesktopSession> findSession(jlong handle) {
    if (handle == 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(gStateMutex);
    const auto found = gSessions.find(handle);
    return found != gSessions.end() ? found->second : nullptr;
}

// Removes the session from the registry and returns it. A second stop of the same
// handle therefore finds nothing and does no work, which is what makes the teardown
// idempotent rather than merely documented as such.
[[nodiscard]] std::shared_ptr<DesktopSession> takeSession(jlong handle) {
    if (handle == 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(gStateMutex);
    const auto found = gSessions.find(handle);
    if (found == gSessions.end()) {
        return nullptr;
    }
    std::shared_ptr<DesktopSession> session = std::move(found->second);
    gSessions.erase(found);
    return session;
}

// The controller is copied out under the session lock so a stop running on another
// thread cannot clear the member under a call that has already started. Every entry
// point that draws or queues goes through this, and every one of them checks for
// null.
[[nodiscard]] std::shared_ptr<NativeCellularityController> liveController(
        const std::shared_ptr<DesktopSession>& session) {
    if (!session) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(session->mutex);
    return session->controller;
}

// Reads a Java string into a std::string. A null argument is refused; a failed
// GetStringUTFChars leaves the OutOfMemoryError the JVM raised pending, which is the
// right thing to propagate, so the caller returns immediately and lets it surface.
[[nodiscard]] bool readUtf8String(
        JNIEnv* env, jstring value, std::string& output) {
    output.clear();
    if (value == nullptr) {
        return false;
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return false;
    }
    output.assign(chars);
    env->ReleaseStringUTFChars(value, chars);
    return true;
}

[[nodiscard]] std::int64_t currentUnixMillis() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
}

// The shared teardown, in the order the standalone host uses and for the same
// reasons: the simulation is stopped first because it owns a thread that writes the
// world the renderer reads; then the surface is detached, which drains the adapter
// installed in the controller; and only then is the session closed.
//
// closeOnOwnerThread is the explicit teardown rather than the destructor, because the
// destructor refuses to release backend resources when it cannot prove it is running
// on the owning thread and leaks them instead. Reaching this function on the calling
// thread is what makes a managed application exit cleanly.
//
// Returns true when every step completed. A failure still consumes the session: the
// caller gets a report rather than a handle it cannot use.
[[nodiscard]] bool tearDownSession(const std::shared_ptr<DesktopSession>& session) {
    bool clean = true;
    std::shared_ptr<NativeCellularityController> controller;
    std::shared_ptr<VulkanBackendSession> backend;
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        controller = std::move(session->controller);
        backend = std::move(session->session);
    }
    if (controller) {
        // noexcept, and it joins the runtime thread, so nothing may run after it that
        // expects the world to still be advancing.
        controller->stopMicroscopeRuntime();
        controller.reset();
    }
    if (backend) {
        (void)backend->detachSurface();
        try {
            backend->closeOnOwnerThread();
        } catch (...) {
            // The backend records why on its own diagnostics; there is nothing here
            // that could act on it, but the caller is told the teardown was not clean.
            clean = false;
        }
        backend.reset();
    }
    return clean;
}

} // namespace

// ---------------------------------------------------------------------------
// Window lifetime
// ---------------------------------------------------------------------------

// Creates the child window the renderer presents into. Returns the HWND as a jlong,
// or 0 if the window could not be created.
//
// The window is a child of the managed side's window rather than a window of its own:
// the application decides where the renderer sits and when it goes away, and the
// WS_CHILD style is what makes Windows clip and move it with its parent. No border or
// caption style is requested, so the client area is the whole window and the extent
// the session is attached with is the extent that is visible.
//
// The window belongs to the calling thread's message queue. Nothing here needs that
// thread to pump messages -- the swapchain and the renderer present directly -- but a
// caller that both creates and drives the window from a thread that never pumps is
// choosing to leave that thread's queue unserviced.
extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeCreateWindow(
        JNIEnv*, jclass, jlong ownerHwnd, jint x, jint y, jint width, jint height) {
    try {
        const HWND owner = windowFromHandle(ownerHwnd);
        if (owner == nullptr || ::IsWindow(owner) == 0) {
            return 0;
        }
        // A zero-extent swapchain is not a meaningful request and a zero-extent child
        // window cannot be presented into, so it is refused here rather than allowed
        // to fail later inside the backend.
        if (width <= 0 || height <= 0) {
            return 0;
        }
        if (!ensureChildWindowClassRegistered()) {
            return 0;
        }
        const HWND child = ::CreateWindowExW(0, kChildWindowClassName, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, x, y, width, height, owner,
                nullptr, moduleInstance(), nullptr);
        if (child == nullptr) {
            return 0;
        }
        registerChildWindow(child);
        return handleFromWindow(child);
    } catch (...) {
        return 0;
    }
}

// Moves and resizes the child window to (0, 0, width, height) inside its parent.
// Returns 0 on success, 1 if the request was refused or Win32 refused it, and 2 if
// the handle is not a live window this bridge created.
//
// This resizes the window, and only the window. The Vulkan swapchain belongs to the
// session, which this entry point is not given, so a caller that needs the presented
// extent to follow the window has to recreate the session at the new size. Leaving it
// as a window-only operation keeps the two handles independent and keeps this call
// safe to make before a session exists.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeResizeWindow(
        JNIEnv*, jclass, jlong windowHandle, jint width, jint height) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (!isBridgeWindow(window)) {
            return kStatusInvalidHandle;
        }
        if (width <= 0 || height <= 0) {
            return kStatusFailed;
        }
        if (::SetWindowPos(window, nullptr, 0, 0, width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE)
                == 0) {
            return kStatusFailed;
        }
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// Destroys the child window. Returns 0 when there was nothing to do (a null handle)
// and when the window is gone, 1 if the window still has a live session or Win32
// refused to destroy it, and 2 if the handle is not a live window this bridge
// created.
//
// The live-session refusal is deliberate. The backend presents into this HWND, so
// destroying it under a running session breaks the surface in a way that surfaces
// later as an unexplained rendering failure; refusing names the order the caller got
// wrong instead. Stop the session first.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeDestroyWindow(
        JNIEnv*, jclass, jlong windowHandle) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (window == nullptr) {
            return kStatusOk;
        }
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            if (gChildWindows.find(window) == gChildWindows.end()) {
                return kStatusInvalidHandle;
            }
            // A session's window member is fixed once the session is published, so
            // this read needs no other lock.
            for (const auto& entry : gSessions) {
                if (entry.second != nullptr && entry.second->window == window) {
                    return kStatusFailed;
                }
            }
        }
        // DestroyWindow is called outside the registry lock: it dispatches messages,
        // and a window procedure that ever reached back into the registry would
        // deadlock against a lock held across it.
        if (::DestroyWindow(window) == 0) {
            return kStatusFailed;
        }
        std::lock_guard<std::mutex> lock(gStateMutex);
        gChildWindows.erase(window);
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// ---------------------------------------------------------------------------
// Session lifetime
// ---------------------------------------------------------------------------

// The simulation start, shared by the two names it is exported under. It is a plain
// file-static function rather than an entry point so the alias below cannot drift from
// the real one: there is exactly one implementation behind both symbols.
//
// The body is the part of the standalone host's main() between obtaining the backend
// API and the first frame, in the same order and with the same values: it creates the
// backend session against the two asset roots, attaches the window, builds the
// default environment with random seeding, validates it for the allocation it is
// about to make, constructs the controller, stamps the world's creation time and
// carries the environment's runtime fields across, installs the Vulkan render surface
// adapter, tells the controller what it is projecting into, selects the Observe
// temperature mode -- the mode that runs the world at a watchable speed -- and starts
// the runtime. The first frame is not drawn here; the managed side drives that with
// nativeRenderFrame on this same thread.
//
// Every failure path releases what it already created: a session that is left
// attached holds a device and a swapchain, and the JVM has no way to reach it once
// this returns 0.
[[nodiscard]] static jlong startSession(JNIEnv* env, jlong windowHandle, jint width,
        jint height, jint maxCellCount, jint maxFoodCount, jdouble dishRadius,
        jstring shaderRoot, jstring textureRoot) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (!isBridgeWindow(window)) {
            return 0;
        }
        if (width <= 0 || height <= 0) {
            return 0;
        }
        std::string shaderRootValue;
        std::string textureRootValue;
        if (!readUtf8String(env, shaderRoot, shaderRootValue)
                || !readUtf8String(env, textureRoot, textureRootValue)) {
            return 0;
        }

        std::shared_ptr<VulkanBackendSession> session;
        std::shared_ptr<NativeCellularityController> controller;
        try {
            // The backend is a separate library, so it is obtained through the same
            // v1 entry point a client uses rather than by linking its internals.
            const CellularityVulkanSurfaceApiV1* backendApi =
                    cellularity_vulkan_get_surface_api_v1();
            if (backendApi == nullptr) {
                return 0;
            }
            session = VulkanBackendSession::create(backendApi,
                    VulkanBackendAssetSource::fromFilesystem(
                            std::move(shaderRootValue),
                            std::move(textureRootValue)));
            if (session == nullptr) {
                return 0;
            }
            if (session->attachWindow(window, width, height)
                    != CELLULARITY_VULKAN_OPERATION_OK_V1) {
                // attachWindow reports why through the backend's own diagnostics; the
                // session still exists and still owns its device, so it is closed
                // rather than dropped.
                (void)session->detachSurface();
                session->closeOnOwnerThread();
                return 0;
            }

            Environment environment = Environment::constructDefaultEnvironment(
                    maxCellCount, maxFoodCount, dishRadius);
            // A fresh substrate with no cells is a valid world but a poor first
            // impression, and the application's own "new substrate" path seeds life
            // the same way.
            environment.addRandom = true;
            cellularity::snapshot_codec::validateEnvironmentForAllocation(
                    environment);

            controller = std::make_shared<NativeCellularityController>(
                    false, environment);
            {
                auto access = controller->writeWorld();
                access.world().mCreatedAtUnixMillis = currentUnixMillis();
                cellularity::snapshot_codec::preserveEnvironmentRuntimeFields(
                        access.world(), environment);
            }
            controller->installRenderSurfaceSession(
                    std::make_unique<cellularity::render::abi::
                                    VulkanRenderSurfaceSessionAdapter>(session));
            // The extent the simulation projects into and converts screen
            // coordinates through. It has to be the child window's client area, which
            // for a window with no border is exactly the size it was created with.
            controller->setViewportSurfaceSize(width, height);
            controller->setMicroscopeTemperatureMode(1);
            controller->startMicroscopeRuntime();
        } catch (...) {
            // Partial start-up. The runtime is stopped before the controller is
            // released so its thread cannot outlive the world it steps, and the
            // surface is detached before the session is closed.
            if (controller) {
                controller->stopMicroscopeRuntime();
                controller.reset();
            }
            if (session) {
                (void)session->detachSurface();
                try {
                    session->closeOnOwnerThread();
                } catch (...) {
                }
                session.reset();
            }
            return 0;
        }

        auto handle = std::make_shared<DesktopSession>();
        handle->window = window;
        handle->session = std::move(session);
        handle->controller = std::move(controller);

        try {
            std::lock_guard<std::mutex> lock(gStateMutex);
            const jlong key = gNextSessionHandle++;
            gSessions.emplace(key, handle);
            return key;
        } catch (...) {
            // Publishing failed, which leaves a fully started session owned by
            // nothing. It is torn down here rather than in the catch above, because
            // by this point the handle owns it.
            (void)tearDownSession(handle);
            return 0;
        }
    } catch (...) {
        return 0;
    }
}

// Creates the simulation and attaches it to the child window. Returns an opaque
// session handle, or 0 if anything failed.
extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeStartSession(
        JNIEnv* env, jclass, jlong windowHandle, jint width, jint height,
        jint maxCellCount, jint maxFoodCount, jdouble dishRadius, jstring shaderRoot,
        jstring textureRoot) {
    return startSession(env, windowHandle, width, height, maxCellCount, maxFoodCount,
            dishRadius, shaderRoot, textureRoot);
}

// The same entry point under the second name the managed side uses for it. The
// declaration in DesktopNativeRenderer.kt is currently called nativeCreateSession,
// while the task that specified this bridge named the symbol nativeStartSession;
// exporting both costs one jump and removes a failure mode that is invisible until the
// first call, because the JVM resolves external functions lazily and reports a name
// mismatch as an UnsatisfiedLinkError rather than at load time.
extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeCreateSession(
        JNIEnv* env, jclass, jlong windowHandle, jint width, jint height,
        jint maxCellCount, jint maxFoodCount, jdouble dishRadius, jstring shaderRoot,
        jstring textureRoot) {
    return startSession(env, windowHandle, width, height, maxCellCount, maxFoodCount,
            dishRadius, shaderRoot, textureRoot);
}

// Draws the newest frame the simulation has published. Returns 0 when the outcome is
// a frame the caller should simply try again from (Presented, Deferred,
// SwapchainRecreated, SkippedNotReady), 1 when it is Failed or SurfaceLost, and 2
// when the handle is not a live session.
//
// This is the whole of the calling thread's job while a session runs: the simulation
// steps the world on its own thread and this presents whatever it has ready, so a
// skipped frame is the normal state between captures and never a fault.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeRenderFrame(
        JNIEnv*, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kStatusInvalidHandle;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kStatusInvalidHandle;
        }
        const RenderFrameOutcome outcome =
                controller->drawMicroscopeForCurrentMode().outcome;
        if (outcome == RenderFrameOutcome::Failed
                || outcome == RenderFrameOutcome::SurfaceLost) {
            return kStatusFailed;
        }
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// Queues a pan of the viewport by a screen-space delta. Returns 1 if the controller
// accepted it, 0 if it refused it or the handle is not a live session.
//
// Nothing here resolves the gesture: the simulation owns the camera, and it applies
// the delta against the viewport it holds at the moment it can safely do so. A
// refusal is therefore an ordinary answer -- it means the input queue is closed --
// and not an error.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeQueuePan(
        JNIEnv*, jclass, jlong sessionHandle, jfloat dx, jfloat dy) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kInputRefused;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kInputRefused;
        }
        return controller->queueMicroscopePan(dx, dy) ? kInputQueued
                                                      : kInputRefused;
    } catch (...) {
        return kInputRefused;
    }
}

// Queues a zoom by a multiplicative factor about a client-area anchor. Returns 1 if
// the controller accepted it, 0 otherwise. The anchor is in client-area pixels with
// the origin at the top left, which is the space the simulation expects because the
// surface extent it was given is the client area.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeQueueZoom(
        JNIEnv*, jclass, jlong sessionHandle, jfloat factor, jfloat screenX,
        jfloat screenY) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kInputRefused;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kInputRefused;
        }
        return controller->queueMicroscopeZoom(factor, screenX, screenY)
                ? kInputQueued
                : kInputRefused;
    } catch (...) {
        return kInputRefused;
    }
}

// Queues a tap at a client-area position. Returns 1 if it was queued, 0 if the handle
// is not a live session.
//
// The genome is deliberately null. A tap with no genome is the gesture that selects
// or deselects whatever is under the cursor, which is the whole of what clicking the
// plate means; placement, which does carry a genome, is a separate managed flow this
// bridge does not serve yet.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeQueueTap(
        JNIEnv*, jclass, jlong sessionHandle, jfloat screenX, jfloat screenY) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kInputRefused;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kInputRefused;
        }
        controller->queueMicroscopeTap(screenX, screenY, nullptr);
        return kInputQueued;
    } catch (...) {
        return kInputRefused;
    }
}

// ---------------------------------------------------------------------------
// Simulation control
// ---------------------------------------------------------------------------

// Selects the microscope temperature mode. Returns 0 on success, 1 if the controller
// refused it, and 2 if the handle is not a live session.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeSetTemperatureMode(
        JNIEnv*, jclass, jlong sessionHandle, jint mode) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kStatusInvalidHandle;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kStatusInvalidHandle;
        }
        controller->setMicroscopeTemperatureMode(mode);
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// Selects the active tool by ordinal. Returns 0 on success, 1 if the controller
// refused it, and 2 if the handle is not a live session.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeSetToolOrdinal(
        JNIEnv*, jclass, jlong sessionHandle, jint ordinal) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kStatusInvalidHandle;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kStatusInvalidHandle;
        }
        controller->setMicroscopeToolOrdinal(ordinal);
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// Resets the world and returns the view with it, which is what the reset control
// promises. Returns 0 on success, 1 if the controller refused it, and 2 if the handle
// is not a live session.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeResetWorld(
        JNIEnv*, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return kStatusInvalidHandle;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return kStatusInvalidHandle;
        }
        controller->resetMicroscopeWorld();
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// The number of live cells in the newest published simulation state, or 0 when the
// handle is not a live session. Zero cells and an invalid handle are therefore
// indistinguishable here, which is acceptable because the value is a readout: a
// caller that needs to tell them apart has the status-returning calls for that.
extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeCellCount(
        JNIEnv*, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return 0;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return 0;
        }
        return static_cast<jlong>(controller->microscopeRuntimeSnapshot().cellCount);
    } catch (...) {
        return 0;
    }
}

// Stops the simulation and releases the session. Returns 0 when the session was
// stopped or there was nothing to stop, and 1 when the teardown reported a failure.
//
// Safe to call twice and safe with 0: the handle is removed from the registry before
// any of the teardown runs, so a second call with the same handle finds nothing,
// reports success and touches no memory that has been freed. That is the contract a
// managed shutdown path needs, because it runs from more than one place -- a window
// closing, a renderer failing, an application exiting.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeStopSession(
        JNIEnv*, jclass, jlong sessionHandle) {
    try {
        const std::shared_ptr<DesktopSession> session = takeSession(sessionHandle);
        if (!session) {
            return kStatusOk;
        }
        return tearDownSession(session) ? kStatusOk : kStatusFailed;
    } catch (...) {
        return kStatusFailed;
    }
}
