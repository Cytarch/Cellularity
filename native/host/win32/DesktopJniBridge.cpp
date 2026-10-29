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
// A third shape of caller is served as well: inspection. The entry points at the bottom of
// this file hand the managed side the wire documents the runtime already writes and reads
// the status it already publishes, so that the editor's model of the environment and the
// simulation's are one schema rather than two that have to be kept in agreement by hand.
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

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "app/NativeCellularityController.h"
// microscopeRuntimeSnapshot() returns this by value, so the controller's forward
// declaration is not enough to read the cell count out of it.
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "core/Environment.h"
#include "core/SimulationMemoryPolicy.h"
#include "core/SimulationWorld.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/NativeSubstrateLoader.h"
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
// Bounded so a pathological window chain cannot turn this into an unbounded walk. Real
// hierarchies are a handful deep; anything longer is not a window tree a click could reach.
constexpr int kMaximumAncestorHops = 64;

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
// Whether every window above this one is shown.
//
// IsWindowVisible answers for one window and ignores the chain it hangs from, and a child of a
// hidden frame reports itself as visible while appearing nowhere. Asking the whole chain is the
// difference between "this window is shown" and "this window can be seen".
bool allAncestorsVisible(HWND window) noexcept {
    HWND current = window;
    for (int hop = 0; hop < 64 && current != nullptr; ++hop) {
        if (::IsWindowVisible(current) == 0) {
            return false;
        }
        current = ::GetParent(current);
    }
    return true;
}

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

    // The extent the window has been given but the surface has not taken yet. The
    // window can be resized from any thread -- the managed side does it from the one
    // that lays out its interface -- while the surface can only be resized from the
    // thread that presents, which is the one calling renderFrame. Recording the
    // request here and applying it at the top of a frame is what keeps the two apart
    // without a lock around the backend, and it also means a refused resize is simply
    // retried on the next frame rather than being lost.
    std::atomic<int> pendingSurfaceWidth{0};
    std::atomic<int> pendingSurfaceHeight{0};

    // The extent the surface was last told about. Only the thread that presents touches
    // these, which is what makes them readable without a lock while the pending values
    // above are written from whichever thread resized the window.
    int appliedSurfaceWidth = 0;
    int appliedSurfaceHeight = 0;

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

[[nodiscard]] std::shared_ptr<DesktopSession> findSessionByWindow(HWND window) {
    if (window == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(gStateMutex);
    for (const auto& entry : gSessions) {
        if (entry.second && entry.second->window == window) {
            return entry.second;
        }
    }
    return nullptr;
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

// Copies a native byte vector into a freshly allocated Java array. Null means the JVM
// refused to allocate it, and the OutOfMemoryError it raised is left pending so the
// caller returns immediately rather than stacking a second failure on top of it.
//
// The bytes are copied verbatim and are never interpreted here. The documents that
// travel through this bridge are written big-endian (ByteWriter in io/NativeByteStream.h
// shifts the most significant byte out first), and the decoder lives in the managed
// application; a second reader on this side would be a second schema to keep in step.
[[nodiscard]] jbyteArray toJavaByteArray(
        JNIEnv* env, const std::vector<std::uint8_t>& bytes) noexcept {
    // A vector longer than a Java array can describe is refused rather than truncated.
    // Every document on this path is bounded by EnvironmentBatch::MAX_WIRE_BYTES, so
    // this cannot happen for a real one; a silently shortened document would be worse
    // than a null, because the decoder would report a schema fault instead of a failure
    // to copy.
    if (bytes.size()
            > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) {
        return nullptr;
    }
    const jsize length = static_cast<jsize>(bytes.size());
    jbyteArray result = env->NewByteArray(length);
    if (result == nullptr) {
        return nullptr;
    }
    if (length > 0) {
        env->SetByteArrayRegion(result, 0, length,
                reinterpret_cast<const jbyte*>(bytes.data()));
    }
    return result;
}

// Reads a Java byte array into a std::vector, verbatim. False means the argument was
// null, or the copy could not be made, and the caller answers null rather than throwing:
// the managed side sends a patch as bytes, and a malformed argument is a refusal, not a
// stack trace. The bytes are not validated here for the same reason they are not
// validated on the way out -- the schema's one reader is the native side of the patch,
// which reports a rejection document rather than an exception.
[[nodiscard]] bool readJavaByteArray(
        JNIEnv* env, jbyteArray source, std::vector<std::uint8_t>& output) {
    output.clear();
    if (source == nullptr) {
        return false;
    }
    const jsize length = env->GetArrayLength(source);
    if (length < 0) {
        return false;
    }
    try {
        output.resize(static_cast<std::size_t>(length));
    } catch (...) {
        return false;
    }
    if (length > 0) {
        env->GetByteArrayRegion(source, 0, length,
                reinterpret_cast<jbyte*>(output.data()));
        // The length check above makes an out-of-range copy impossible, but a pending
        // exception would mean the vector is partly filled and the JVM expects the
        // caller to stop, so the refusal is explicit rather than assumed.
        if (env->ExceptionCheck() != JNI_FALSE) {
            return false;
        }
    }
    return true;
}

// Rounds a native double to the whole number the managed status vector carries, which
// has one jlong per value. Non-finite values saturate instead of trapping, because this
// runs on a path whose contract is that nothing throws: a NaN round-tripped into a
// status readout would otherwise be undefined behaviour rather than a readout.
[[nodiscard]] jlong roundedToWhole(double value) noexcept {
    constexpr double kMaximumWhole =
            static_cast<double>(std::numeric_limits<std::int64_t>::max());
    constexpr double kMinimumWhole =
            static_cast<double>(std::numeric_limits<std::int64_t>::min());
    if (std::isnan(value)) {
        return 0;
    }
    if (value >= kMaximumWhole) {
        return std::numeric_limits<jlong>::max();
    }
    if (value <= kMinimumWhole) {
        return std::numeric_limits<jlong>::min();
    }
    return static_cast<jlong>(std::llround(value));
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
// Estimates the peak memory one world of this shape needs.
//
// Returns the estimate in bytes, or -1 when the world would not fit in the budget this
// machine grants it. The distinction matters to the caller and is not a detail: the freezer
// uses the number to show what starting a plate will cost, and uses the refusal to disable
// the start button rather than letting a person launch a world that the runtime will then
// reject. Returning zero for a refusal would say "this costs nothing", which is the one
// answer that would make the screen lie.
//
// This mirrors the entry point the Android bridge exposes, deliberately and completely: the
// same policy, the same environment construction, the same two answers. A desktop that
// estimated differently from a phone would be a second answer to a question that has one.
extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeEstimateWorldAllocationBytes(
        JNIEnv*, jclass, jint maxCellCount, jint maxFoodCount, jdouble radius) {
    try {
        const Environment environment = Environment::constructDefaultEnvironment(
                maxCellCount, maxFoodCount, radius);
        const cellularity::memory::WorldAllocationEstimate estimate =
                cellularity::memory::estimateCurrentWorldAllocation(environment);
        return estimate.bytes <= estimate.budgetBytes
                ? static_cast<jlong>(estimate.bytes)
                : static_cast<jlong>(-1);
    } catch (...) {
        return static_cast<jlong>(-1);
    }
}

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
// Describes how this process relates to the display's scaling, and how large the child
// window's client area actually is, as one string.
//
// This is the fact that decides whether any of the other numbers mean what they appear to.
// A process that declares itself aware of the display's scaling is given real pixels by every
// Win32 call; one that does not is given virtualised ones, and the same value then means two
// different things depending on who is asked. A window sized from one space and hit-tested in
// the other lands somewhere neither side intended -- and every attempt to reason about it from
// the managed side alone has produced a confident answer that the next measurement contradicted.
extern "C" JNIEXPORT jstring JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeDescribeScaling(
        JNIEnv* env, jclass, jlong windowHandle) {
    try {
        std::string report;
        const DPI_AWARENESS_CONTEXT context = ::GetThreadDpiAwarenessContext();
        const DPI_AWARENESS awareness = ::GetAwarenessFromDpiAwarenessContext(context);
        report += "awareness=";
        switch (awareness) {
            case DPI_AWARENESS_UNAWARE:
                report += "unaware";
                break;
            case DPI_AWARENESS_SYSTEM_AWARE:
                report += "system";
                break;
            case DPI_AWARENESS_PER_MONITOR_AWARE:
                report += "per-monitor";
                break;
            default:
                report += "unknown";
                break;
        }
        report += " systemDpi=";
        report += std::to_string(::GetDpiForSystem());
        const HWND window = windowFromHandle(windowHandle);
        if (window != nullptr && isBridgeWindow(window)) {
            const UINT windowDpi = ::GetDpiForWindow(window);
            report += " windowDpi=";
            report += std::to_string(windowDpi);
            // Whether this window and every one above it are shown. A child is only on screen if
            // its ancestors are, so a check that stopped at the child would report a window as
            // visible while the thing it sits in is not -- and a picture nobody can see is the
            // same to a person as a picture that was never drawn.
            report += " selfVisible=";
            report += ::IsWindowVisible(window) != 0 ? "yes" : "no";
            report += " allAncestorsVisible=";
            report += allAncestorsVisible(window) ? "yes" : "no";
            RECT onScreen{};
            if (::GetWindowRect(window, &onScreen) != 0) {
                report += " windowRect=";
                report += std::to_string(onScreen.left);
                report += ",";
                report += std::to_string(onScreen.top);
            }

            RECT client{};
            if (::GetClientRect(window, &client) != 0) {
                report += " childClient=";
                report += std::to_string(client.right - client.left);
                report += "x";
                report += std::to_string(client.bottom - client.top);
            }
            RECT bounds{};
            if (::GetWindowRect(window, &bounds) != 0) {
                report += " childWindow=";
                report += std::to_string(bounds.right - bounds.left);
                report += "x";
                report += std::to_string(bounds.bottom - bounds.top);
            }
            const HWND parent = ::GetParent(window);
            // Where the child sits inside its parent, which is the number that decides whether it
            // covers something the interface drew. A child window is in front of everything the
            // interface paints, so a viewport one row too high is a viewport on top of the bar
            // beneath it -- and the two rectangles being the same size says nothing about it.
            //
            // Both sides of this subtraction are Win32 screen coordinates, so no scale is involved
            // and none is applied. Adding one is what made an earlier measurement of this kind
            // meaningless.
            if (parent != nullptr) {
                RECT parentBounds{};
                if (::GetWindowRect(parent, &parentBounds) != 0) {
                    report += " offsetInParent=";
                    report += std::to_string(onScreen.left - parentBounds.left);
                    report += ",";
                    report += std::to_string(onScreen.top - parentBounds.top);
                    report += " parentSize=";
                    report += std::to_string(parentBounds.right - parentBounds.left);
                    report += "x";
                    report += std::to_string(parentBounds.bottom - parentBounds.top);
                }
            }
            if (parent != nullptr) {
                RECT parentClient{};
                if (::GetClientRect(parent, &parentClient) != 0) {
                    report += " parentClient=";
                    report += std::to_string(parentClient.right - parentClient.left);
                    report += "x";
                    report += std::to_string(parentClient.bottom - parentClient.top);
                }
            }
        }
        return env->NewStringUTF(report.c_str());
    } catch (...) {
        return nullptr;
    }
}
// Shows or hides the child window. Returns 0 on success, 1 if Win32 refused, and 2 if the
// handle is not a live window this bridge created.
//
// A child window is not clipped by what the interface draws, so it is in front of the
// interface wherever it lies -- which is what lets the simulation be visible at all, and also
// what made it cover every other tab. The surface stays mounted while another tab is showing
// because it owns the Vulkan device and the simulation thread, and tearing it down to hide a
// picture would stop the world and start a different one when the tab came back. Hiding the
// window is what keeps the session alive without keeping the picture in front of everything.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeShowWindow(
        JNIEnv*, jclass, jlong windowHandle, jboolean visible) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (!isBridgeWindow(window)) {
            return kStatusInvalidHandle;
        }
        if (::ShowWindow(window, visible != 0 ? SW_SHOWNA : SW_HIDE) == 0) {
            // ShowWindow reports the previous visibility rather than success, so a zero here
            // only means the window was already hidden. The call itself cannot fail for a
            // window this bridge created.
        }
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}
// Reports the extent the surface is presenting at, as {width, height}, or null when the
// handle is not a live session or no frame has been presented yet.
//
// This is the number the picture is actually drawn into, and it is deliberately not the
// window's size: the swapchain is resized from the thread that presents, so between a window
// resize and the next frame the two disagree, and a session that never got the resize keeps
// presenting into its creation extent for the rest of its life. A window larger than the
// surface it presents is a picture that fills part of its own viewport, which is worth being
// able to measure rather than infer from a screenshot.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeSurfaceExtent(
        JNIEnv* env, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return nullptr;
    }
    // Both fields belong to the thread that presents and are read here from a caller that is
    // not that thread. That is deliberate and safe in the direction it is used: a probe
    // comparing them against the window wants to know whether the surface has caught up, and
    // a stale reading can only ever report an extent that was true a frame ago.
    const jlong values[2] = {
            static_cast<jlong>(session->appliedSurfaceWidth),
            static_cast<jlong>(session->appliedSurfaceHeight)};
    if (values[0] <= 0 || values[1] <= 0) {
        return nullptr;
    }
    jlongArray result = env->NewLongArray(2);
    if (result == nullptr) {
        return nullptr;
    }
    env->SetLongArrayRegion(result, 0, 2, values);
    return result;
}

// Captures one of this bridge's windows into a 32-bit top-down BMP file and returns the byte
// count written, or -1 when the handle is not a live window this bridge created or the file
// could not be written.
//
// This is for looking at the renderer's output without looking at the machine. The usual way
// to check what a window looks like is a screen capture, which on a shared machine means
// taking the screen away from whoever is using it, and which also photographs whatever
// happens to be in front. Capturing one window by its handle answers the same question about
// the same pixels and touches nothing else: the file holds this window's contents or an
// error, and no part of anybody else's screen is read.
//
// PW_RENDERFULLCONTENT is what makes a window that is not on a monitor come out with its
// contents rather than as a blank rectangle: a Vulkan surface presents into a composited
// child window, and the ordinary path would ask the desktop compositor for pixels that were
// never put on a monitor.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeCaptureWindow(
        JNIEnv* env, jclass, jlong windowHandle, jstring path) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (!isBridgeWindow(window)) {
            return -1;
        }
        std::string destination;
        if (!readUtf8String(env, path, destination) || destination.empty()) {
            return -1;
        }

        RECT bounds{};
        if (::GetWindowRect(window, &bounds) == 0) {
            return -1;
        }
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) {
            return -1;
        }

        HDC windowDc = ::GetWindowDC(window);
        if (windowDc == nullptr) {
            return -1;
        }
        HDC memoryDc = ::CreateCompatibleDC(windowDc);
        HBITMAP bitmap = memoryDc == nullptr
                ? nullptr
                : ::CreateCompatibleBitmap(windowDc, width, height);
        if (memoryDc == nullptr || bitmap == nullptr) {
            if (bitmap != nullptr) {
                ::DeleteObject(bitmap);
            }
            if (memoryDc != nullptr) {
                ::DeleteDC(memoryDc);
            }
            ::ReleaseDC(window, windowDc);
            return -1;
        }


        // A blit, and PrintWindow is not tried here.
        //
        // PrintWindow was attempted once the window started being owned by the thread that reads
        // its messages, on the reasoning that a request the compositor never answers could now be
        // answered. It still hangs. The window it was asked about sits on no monitor, and the
        // compositor has nothing to hand back for one -- so the request waits rather than failing,
        // and a function that waits forever inside a bridge is worse than one that returns an
        // empty picture.
        //
        // What that costs is stated plainly: a blit copies the window's own surface, and content
        // drawn by the GPU may never have been in it, so a capture can come back as one flat
        // colour. An empty capture is reported as an empty capture and not as a picture of a black
        // world, and the caller decides what it means. Nothing in this project should be judged by
        // such a picture, and the honest place for that sentence is here, next to the call.
        const HGDIOBJ previous = ::SelectObject(memoryDc, bitmap);
        const BOOL rendered = ::BitBlt(memoryDc, 0, 0, width, height, windowDc, 0, 0,
                SRCCOPY | CAPTUREBLT);        ::SelectObject(memoryDc, previous);

        // Read back top-down so the file can be walked row by row by anything that reads a
        // BMP, without the reader having to know which way round the rows are.
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const std::size_t pixelBytes =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
        std::vector<std::uint8_t> pixels(pixelBytes);
        const int scanned = ::GetDIBits(memoryDc, bitmap, 0, static_cast<UINT>(height),
                pixels.data(), &info, DIB_RGB_COLORS);

        ::DeleteObject(bitmap);
        ::DeleteDC(memoryDc);
        ::ReleaseDC(window, windowDc);

        if (rendered == 0 || scanned == 0) {
            return -1;
        }

        const std::size_t headerBytes = 14U + 40U;
        std::vector<std::uint8_t> file;
        file.reserve(headerBytes + pixelBytes);
        const auto append16 = [&file](std::uint16_t value) {
            file.push_back(static_cast<std::uint8_t>(value & 0xFFU));
            file.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        };
        const auto append32 = [&file](std::uint32_t value) {
            file.push_back(static_cast<std::uint8_t>(value & 0xFFU));
            file.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
            file.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
            file.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
        };
        const std::uint32_t fileSize =
                static_cast<std::uint32_t>(headerBytes + pixelBytes);
        file.push_back('B');
        file.push_back('M');
        append32(fileSize);
        append16(0);
        append16(0);
        append32(static_cast<std::uint32_t>(headerBytes));
        append32(40U);
        append32(static_cast<std::uint32_t>(width));
        append32(static_cast<std::uint32_t>(height));
        append16(1);
        append16(32);
        append32(0);
        append32(static_cast<std::uint32_t>(pixelBytes));
        append32(2835);
        append32(2835);
        append32(0);
        append32(0);
        file.insert(file.end(), pixels.begin(), pixels.end());

        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) {
            return -1;
        }
        output.write(reinterpret_cast<const char*>(file.data()),
                static_cast<std::streamsize>(file.size()));
        if (!output) {
            return -1;
        }
        return static_cast<jint>(file.size());
    } catch (...) {
        return -1;
    }
}
// Narrows a wide Win32 string for a report. Window class names and titles are UTF-16 and the
// report is read by a person, so anything that cannot be represented is replaced rather than
// dropped: a missing character in a class name would be more confusing than a substituted one.
[[nodiscard]] std::string narrowFromWide(const wchar_t* value) {
    if (value == nullptr) {
        return {};
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string narrowed(static_cast<std::size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value, -1, narrowed.data(), needed, nullptr, nullptr);
    return narrowed;
}
// Describes the top-level windows of this process in their real stacking order, topmost
// first, as one string per window: handle, class name, title and screen rectangle.
//
// A z-order is not something either half of this port can report about itself. Compose knows
// which of its own surfaces is over which; Win32 knows which window is over which; and the
// question "is the simulation underneath the interface" is about the second, asked of the
// windows rather than inferred from the layout. GetTopWindow followed by repeated
// GetWindow(GW_HWNDNEXT) enumerates exactly that order, and a window that appears above the
// renderer in it is a window that takes clicks before the renderer sees them.
extern "C" JNIEXPORT jstring JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeDescribeWindows(
        JNIEnv* env, jclass, jlong ownerProcessWindow) {
    try {
        const DWORD processId = ::GetCurrentProcessId();
        std::string report;
        int index = 0;
        for (HWND window = ::GetTopWindow(nullptr); window != nullptr;
                window = ::GetWindow(window, GW_HWNDNEXT)) {
            DWORD windowProcess = 0;
            ::GetWindowThreadProcessId(window, &windowProcess);
            // Only this process's windows: another application's window is not part of what
            // this port is composed of, and on a machine somebody is using there would be
            // many of them between the relevant ones.
            if (windowProcess != processId) {
                continue;
            }
            wchar_t className[256] = {};
            ::GetClassNameW(window, className, 256);
            wchar_t title[256] = {};
            ::GetWindowTextW(window, title, 256);
            RECT bounds{};
            ::GetWindowRect(window, &bounds);
            report += std::to_string(index);
            report += " hwnd=";
            report += std::to_string(handleFromWindow(window));
            report += " class=";
            report += narrowFromWide(className);
            report += " title=";
            report += narrowFromWide(title);
            report += " rect=";
            report += std::to_string(bounds.left);
            report += ",";
            report += std::to_string(bounds.top);
            report += " ";
            report += std::to_string(bounds.right - bounds.left);
            report += "x";
            report += std::to_string(bounds.bottom - bounds.top);
            report += " visible=";
            report += ::IsWindowVisible(window) != 0 ? "yes" : "no";
            report += "\n";
            index++;
        }
        const jstring result = env->NewStringUTF(report.c_str());
        return result;
    } catch (...) {
        return nullptr;
    }
}
// Reports what Win32 would deliver a click at a screen point to, among the windows this
// bridge made: 1 when the point belongs to one of them, 0 when it belongs to some other
// window inside the same top-level window (which is what the interface drawing over the
// simulation looks like), and -1 when the point is not in that top-level window at all or
// belongs to no window.
//
// This settles a question that cannot be settled from either side alone. The simulation lives
// in a native child window and the interface is drawn by Compose into the same top-level
// window, so whether a click reaches the simulation depends on which of the two Win32
// considers to be on top at that pixel -- and neither system knows about the other's
// windows. Asking Windows directly is the only account of it that is not a guess, and it is
// the difference between "the interface is over the simulation" and "the simulation is over
// the interface and not receiving anything", which have opposite fixes.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeWindowAtPoint(
        JNIEnv*, jclass, jint screenX, jint screenY) {
    try {
        // Screen coordinates in the same space WindowFromPoint answers in, with no conversion.
        // An earlier version scaled them by the system dots per inch on the theory that the
        // caller worked in physical pixels and Windows did not; the measurements then came
        // back as "no window" everywhere, which is what a conversion applied to coordinates
        // that were already correct looks like. GetWindowRect and WindowFromPoint agree with
        // each other, so handing one's answer to the other needs nothing in between.
        POINT point{screenX, screenY};
        const HWND hit = ::WindowFromPoint(point);
        if (hit == nullptr) {
            return -1;
        }
        if (isBridgeWindow(hit)) {
            return 1;
        }
        // Walk up to the top-level window. A point inside a nested editor or a dialog would
        // otherwise be compared against the wrong ancestor and read as "somewhere else".
        HWND top = hit;
        for (int hop = 0; hop < kMaximumAncestorHops; hop++) {
            const HWND parent = ::GetAncestor(top, GA_PARENT);
            if (parent == nullptr || parent == top || ::GetAncestor(parent, GA_PARENT) == nullptr) {
                break;
            }
            top = parent;
        }
        // A bridge window that reaches the point through a child of its own is still the
        // bridge's window, and the walk above may have started inside one.
        if (isBridgeWindow(top)) {
            return 1;
        }
        std::lock_guard<std::mutex> lock(gStateMutex);
        for (const HWND window : gChildWindows) {
            if (::IsWindow(window) != 0 && ::GetAncestor(window, GA_ROOT) == top) {
                return 0;
            }
        }
        return -1;
    } catch (...) {
        return -1;
    }
}

// Reads the child window's rectangle and the client area of its parent.
//
// Returns {x, y, width, height of the child window}, {width, height of the parent's client
// area}, and {the child's x and y relative to the parent's client origin}, or null when the
// handle is not a live window this bridge created.
//
// This exists because "the simulation is drawn into a corner" is a claim about two
// rectangles that are decided by two different systems: the child window's rectangle is set
// by Win32 calls this bridge makes, and the place reserved for it in the interface is decided
// by the Compose layout. Neither can see the other's numbers, so a disagreement between them
// is invisible from inside either one and shows up only as a picture that is the wrong size
// in the wrong place. Reporting both together is what makes the disagreement measurable.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeWindowGeometry(
        JNIEnv* env, jclass, jlong windowHandle) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (!isBridgeWindow(window)) {
            return nullptr;
        }
        RECT windowRect{};
        if (::GetWindowRect(window, &windowRect) == 0) {
            return nullptr;
        }
        const HWND parent = ::GetParent(window);
        RECT clientRect{};
        POINT clientOrigin{0, 0};
        if (parent != nullptr) {
            if (::GetClientRect(parent, &clientRect) == 0) {
                return nullptr;
            }
            if (::ClientToScreen(parent, &clientOrigin) == 0) {
                return nullptr;
            }
        }
        const jlong values[10] = {
                static_cast<jlong>(windowRect.left),
                static_cast<jlong>(windowRect.top),
                static_cast<jlong>(windowRect.right - windowRect.left),
                static_cast<jlong>(windowRect.bottom - windowRect.top),
                static_cast<jlong>(clientRect.right - clientRect.left),
                static_cast<jlong>(clientRect.bottom - clientRect.top),
                static_cast<jlong>(windowRect.left - clientOrigin.x),
                static_cast<jlong>(windowRect.top - clientOrigin.y),
                // Whether Win32 considers the window visible. A hidden window still has a
                // rectangle and is still reported by GetWindowRect, but WindowFromPoint
                // ignores it entirely -- so a scan that finds no window anywhere is
                // indistinguishable from a scan of a hidden one unless this is known.
                ::IsWindowVisible(window) != 0 ? 1 : 0,
                static_cast<jlong>(::GetParent(window) != nullptr ? 1 : 0)};
        jlongArray result = env->NewLongArray(10);
        if (result == nullptr) {
            return nullptr;
        }
        env->SetLongArrayRegion(result, 0, 10, values);
        return result;
    } catch (...) {
        return nullptr;
    }
}

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
        // Placed at the parent's client origin here, which is only correct while nothing else
        // is competing for that space. The caller that knows where the viewport actually goes
        // moves the window with setChildWindowBounds below; doing it there rather than here
        // keeps this entry point the plain "make it this big" operation its name promises.
        // The window is the new size now but the surface is not, and the surface may
        // only be resized from the thread that presents. The request is recorded for the
        // next frame to pick up, which also means a refusal is retried rather than lost.
        // A window with no session yet has nothing to resize: its session takes its
        // extent from the window at the moment it is created.
        if (const std::shared_ptr<DesktopSession> session =
                    findSessionByWindow(window)) {
            session->pendingSurfaceWidth.store(width, std::memory_order_release);
            session->pendingSurfaceHeight.store(height, std::memory_order_release);
        }
        return kStatusOk;
    } catch (...) {
        return kStatusFailed;
    }
}

// Places the child window so that its top-left corner sits at a screen point, and gives it an
// extent, in one call.
//
// The point arrives in screen coordinates and is converted here, because the conversion is
// from the desktop's coordinate space into the parent's client space and this is the side that
// can ask Win32 to do it. The managed side knows where the interface put the viewport, but it
// cannot know the parent's client origin or how the two coordinate spaces are scaled -- and
// guessing either is how a window ends up over the interface.
//
// Returns 0 on success, 1 if the request was refused, and 2 if the handle is not a live window
// this bridge created.
extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeSetChildWindowBounds(
        JNIEnv*, jclass, jlong windowHandle, jint offsetX, jint offsetY, jint width,
        jint height) {
    try {
        const HWND window = windowFromHandle(windowHandle);
        if (!isBridgeWindow(window)) {
            return kStatusInvalidHandle;
        }
        if (width <= 0 || height <= 0) {
            return kStatusFailed;
        }
        // The offset is taken as already being relative to the parent's client area, and the
        // screen coordinates in the signature are only a cross-check. Converting from the
        // desktop's space was tried first and was wrong in a way worth recording: the managed
        // side works in AWT's logical coordinates while this side sees physical pixels, and
        // under display scaling ScreenToClient turned a viewport offset into a negative one.
        // A caller that knows where the canvas sits inside its AWT parent can state the
        // offset directly, and then no coordinate space has to be reconciled at all.
        POINT origin{offsetX, offsetY};
        // The window is moved and resized, and the surface follows a frame later: the
        // swapchain may only be touched by the thread that presents. Both are recorded the
        // same way a plain resize records them, so this cannot leave the two disagreeing.
        if (::SetWindowPos(window, nullptr, origin.x, origin.y, width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE)
                == 0) {
            return kStatusFailed;
        }
        if (const std::shared_ptr<DesktopSession> session =
                    findSessionByWindow(window)) {
            session->pendingSurfaceWidth.store(width, std::memory_order_release);
            session->pendingSurfaceHeight.store(height, std::memory_order_release);
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
        jstring shaderRoot, jstring textureRoot, jstring substratePath) {
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
        // A null path means "a new plate"; an empty one is a caller mistake rather than a
        // request for the current directory, so it is refused instead of being handed to
        // the decoder.
        std::string substratePathValue;
        const bool openStoredPlate = substratePath != nullptr;
        if (openStoredPlate
                && (!readUtf8String(env, substratePath, substratePathValue)
                        || substratePathValue.empty())) {
            return 0;
        }

        std::shared_ptr<VulkanBackendSession> session;
        std::shared_ptr<NativeCellularityController> controller;
        int appliedSurfaceWidth = 0;
        int appliedSurfaceHeight = 0;
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
            environment.addRandom = true;
            cellularity::snapshot_codec::validateEnvironmentForAllocation(
                    environment);

            if (openStoredPlate) {
                // A saved plate is decoded rather than approximated. The counts above are
                // the plate's own named allocation, so they are the fallback the loader
                // needs if the file predates recording them; the file's world supplies
                // everything else, including when it was created, which is the whole point
                // of opening a stored plate rather than starting a new one.
                NativeSubstrateLoadResult loaded = loadNativeSubstrateFromPath(
                        substratePathValue, /*previewMode=*/false, environment);
                if (loaded.world == nullptr) {
                    return 0;
                }
                if (loaded.world->mCreatedAtUnixMillis <= 0) {
                    loaded.world->mCreatedAtUnixMillis = currentUnixMillis();
                }
                environment = loaded.world->mEnvironment;
                controller = std::make_shared<NativeCellularityController>(
                        std::move(loaded.world));
            } else {
                // A fresh substrate with no cells is a valid world but a poor first
                // impression, and the application's own "new substrate" path seeds life
                // the same way.
                controller = std::make_shared<NativeCellularityController>(
                        false, environment);
                {
                    auto access = controller->writeWorld();
                    access.world().mCreatedAtUnixMillis = currentUnixMillis();
                    cellularity::snapshot_codec::preserveEnvironmentRuntimeFields(
                            access.world(), environment);
                }
            }
            controller->installRenderSurfaceSession(
                    std::make_unique<cellularity::render::abi::
                                    VulkanRenderSurfaceSessionAdapter>(session));
            // The extent the simulation projects into and converts screen
            // coordinates through. It has to be the child window's client area, which
            // for a window with no border is exactly the size it was created with.
            controller->setViewportSurfaceSize(width, height);
            appliedSurfaceWidth = width;
            appliedSurfaceHeight = height;
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
        // The extent the session was created with, so the frame loop does not re-apply
        // it. Only the presenting thread touches these.
        handle->appliedSurfaceWidth = appliedSurfaceWidth;
        handle->appliedSurfaceHeight = appliedSurfaceHeight;

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
            dishRadius, shaderRoot, textureRoot, nullptr);
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
            dishRadius, shaderRoot, textureRoot, nullptr);
}

// The same thing, opening a plate that was saved earlier.
//
// A separate entry point rather than an extra argument on the two above, because the JVM
// resolves these by name and a changed signature is not a compile error on the native side
// -- it is an UnsatisfiedLinkError on the first click. The extra name costs a jump and makes
// the difference visible in the export table.
extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeCreateSessionFromSubstrate(
        JNIEnv* env, jclass, jlong windowHandle, jint width, jint height,
        jint maxCellCount, jint maxFoodCount, jdouble dishRadius, jstring shaderRoot,
        jstring textureRoot, jstring substratePath) {
    return startSession(env, windowHandle, width, height, maxCellCount, maxFoodCount,
            dishRadius, shaderRoot, textureRoot, substratePath);
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

        // A resize asked for since the last frame is applied here, because this is the
        // thread that owns the surface. It is applied whenever the request disagrees with
        // the extent the viewport is actually presenting at, not only when it first
        // arrives, so a resize the surface refused while the window was minimized or
        // mid-drag is retried on later frames instead of leaving the picture stretched
        // for the rest of the session.
        //
        // The viewport is told as well: the extent the simulation projects into and
        // converts taps through has to be the client area, or input lands somewhere other
        // than the cursor.
        const int pendingWidth =
                session->pendingSurfaceWidth.load(std::memory_order_acquire);
        const int pendingHeight =
                session->pendingSurfaceHeight.load(std::memory_order_acquire);
        if (pendingWidth > 0 && pendingHeight > 0
                && (pendingWidth != session->appliedSurfaceWidth
                        || pendingHeight != session->appliedSurfaceHeight)) {
            controller->setViewportSurfaceSize(pendingWidth, pendingHeight);
            controller->invalidateMicroscopeFrameData();
            session->appliedSurfaceWidth = pendingWidth;
            session->appliedSurfaceHeight = pendingHeight;
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

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------
//
// The four calls below are request round-trips rather than reads. Each one goes through
// the controller's owner-request queue and waits for the runtime thread to service it,
// because the world they describe is owned by that thread and the queue is the only way
// to look at it without racing it. They are consequently not frame-loop calls: a caller
// that polls one every frame adds the runtime's service latency to every frame.
//
// None of them can report a status code, because their return value is the payload. A
// refusal is a null, so a caller that needs to tell "no such session" from "nothing to
// report" has to ask a status-returning call instead. Null is also what a runtime that
// is not running produces: these requests require an active runtime and the controller
// reports that refusal by throwing, which the catch below turns into the same null.

// Reads the environment as one CEVS document (magic, schema version, revision, the six
// boolean switches, the three field descriptors, the 23 scalar doubles, the gene pool and
// the mutation weights), or null when the handle is not a live session or the read
// failed.
//
// This is the whole document rather than a chosen subset because the managed side owns
// the decode: the wire schema and the editor's model are one thing, and slicing it here
// would mean a second definition of it in C++ that the next schema change would have to
// remember to update. The bytes are copied unchanged, most significant byte first, which
// is how the native writer emits them.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeReadEnvironment(
        JNIEnv* env, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return nullptr;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return nullptr;
        }
        const std::vector<std::uint8_t> document =
                controller->inspectMicroscopeEnvironment();
        // An empty document is a failure of the read rather than a valid answer: every
        // successful inspection writes the fixed header above, so a zero-length result
        // would only reach the decoder as a schema fault.
        if (document.empty()) {
            return nullptr;
        }
        return toJavaByteArray(env, document);
    } catch (...) {
        return nullptr;
    }
}

// Applies a CEVP patch document and returns the CEVR result document the runtime wrote
// for it, or null when the handle is not a live session or the exchange failed.
//
// A null here is not the same as a refused patch. The runtime answers a patch it will not
// apply with a result document carrying the rejection list, and that document is what
// this returns: the managed side needs to know which change was rejected and why, so
// swallowing a rejection into a null would throw away the only account of it.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeApplyEnvironmentPatch(
        JNIEnv* env, jclass, jlong sessionHandle, jbyteArray patchBytes) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return nullptr;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return nullptr;
        }
        std::vector<std::uint8_t> bytes;
        if (!readJavaByteArray(env, patchBytes, bytes)) {
            return nullptr;
        }
        // The controller returns the result document by value and has already turned a
        // malformed patch into a rejection document, so there is nothing left to check
        // except the shape of the answer: an empty one is the failure case, because
        // every result carries at least the magic, the version and the revision.
        const std::vector<std::uint8_t> result =
                controller->applyMicroscopeEnvironmentPatch(std::move(bytes));
        if (result.empty()) {
            return nullptr;
        }
        return toJavaByteArray(env, result);
    } catch (...) {
        return nullptr;
    }
}

// Reads the runtime's status as eleven longs, in this order: revision, worldEpoch,
// completedUpdateCount, cellCount, foodAsWholeUnits, simulationTimeMillis,
// temperatureMode, toolOrdinal, selectedCellId, failedFlag, diagnosisRequestedFlag. Null
// when the handle is not a live session.
//
// One array rather than eleven calls, because the runtime publishes these fields as one
// snapshot: eleven readings would be eleven different moments, and a status line assembled
// from them would show a cell count from before an update next to a revision from after it.
//
// Two of the eleven are conversions, and both are done here rather than by each caller. The
// snapshot reports food as a total mass, which is the only food figure it carries -- there
// is no food count in it -- and it is rounded to whole units so that a plate with food on
// it does not read as zero. Simulation time is fractional seconds and is reported as whole
// milliseconds, which is the resolution a status line can show.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeReadStats(
        JNIEnv* env, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return nullptr;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return nullptr;
        }
        const cellularity::microscope::NativeMicroscopeRuntimeSnapshot snapshot =
                controller->microscopeRuntimeSnapshot();
        // A negative simulation time cannot be shown as a duration and is reported as
        // zero; a negative food mass is equally meaningless and is left to the rounding
        // below, which preserves its sign rather than hiding it.
        const jlong simulationTimeMillis = snapshot.simulationTime > 0.0
                ? roundedToWhole(snapshot.simulationTime * 1000.0)
                : 0;
        const jlong foodWholeUnits = roundedToWhole(snapshot.foodMass);
        const jlong values[] = {
                static_cast<jlong>(snapshot.revision),
                static_cast<jlong>(snapshot.worldEpoch),
                static_cast<jlong>(snapshot.completedUpdateCount),
                static_cast<jlong>(snapshot.cellCount),
                foodWholeUnits,
                simulationTimeMillis,
                static_cast<jlong>(snapshot.temperatureMode),
                static_cast<jlong>(snapshot.toolOrdinal),
                static_cast<jlong>(snapshot.selectedCellId),
                // The managed declaration reads this as a flag, so a runtime that has
                // failed reports 1 rather than the C++ bool's byte.
                snapshot.failed ? 1 : 0,
                // Whether the runtime is currently answering the question "what is this
                // cell". It is true only while the diagnosis tool is selected and a cell is
                // selected, which is exactly when a snapshot was written -- so a caller that
                // reads it as a flag never asks for a document that was never produced.
                snapshot.diagnosisRequested ? 1 : 0,
        };
        constexpr jsize kValueCount =
                static_cast<jsize>(sizeof(values) / sizeof(values[0]));
        jlongArray result = env->NewLongArray(kValueCount);
        if (result == nullptr) {
            return nullptr;
        }
        env->SetLongArrayRegion(result, 0, kValueCount, values);
        return result;
    } catch (...) {
        return nullptr;
    }
}

// Reads one cell as a document the managed side decodes, or null when the handle is not a
// live session, the runtime has no such cell, or the read failed.
//
// The empty case is a real answer and not an error: a cell that has died since the call
// that named it is the ordinary way this returns nothing, which is why the controller
// hands back an optional rather than an empty vector, and why the null is passed on
// instead of being turned into a zero-length array that a decoder would have to
// distinguish from a real document.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeInspectCell(
        JNIEnv* env, jclass, jlong sessionHandle, jint cellId) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return nullptr;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return nullptr;
        }
        const std::optional<std::vector<std::uint8_t>> document =
                controller->inspectMicroscopeCell(static_cast<int>(cellId));
        if (!document.has_value() || document->empty()) {
            return nullptr;
        }
        return toJavaByteArray(env, *document);
    } catch (...) {
        return nullptr;
    }
}

// Reads the selected cell's genome as the same document the phone reads, or null when the
// handle is not a live session, nothing is selected, or the read failed.
//
// The genome is a separate document from the cell snapshot because the runtime publishes them
// separately and for different reasons: the cell changes every frame, the genome does not, and
// the diagnosis panel shows both because they describe the same organism. Returning them as
// one document would mean re-sending a genome whenever a cell moved.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_nativeInspectSelectedGenome(
        JNIEnv* env, jclass, jlong sessionHandle) {
    const std::shared_ptr<DesktopSession> session = findSession(sessionHandle);
    if (!session) {
        return nullptr;
    }
    try {
        const std::shared_ptr<NativeCellularityController> controller =
                liveController(session);
        if (!controller) {
            return nullptr;
        }
        const std::optional<std::vector<std::uint8_t>> genome =
                controller->inspectMicroscopeSelectedGenome();
        if (!genome.has_value() || genome->empty()) {
            return nullptr;
        }
        return toJavaByteArray(env, *genome);
    } catch (...) {
        return nullptr;
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
