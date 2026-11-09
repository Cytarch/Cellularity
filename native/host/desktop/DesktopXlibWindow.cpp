#include "host/desktop/DesktopXlibWindow.h"

#include <mutex>
#include <new>

#include <X11/Xlib.h>

namespace cellularity::desktop {
namespace {

using cellularity::render::vulkan::XlibWindowHandle;

// One connection for every window the bridge creates, separate from the toolkit's.
// Every Xlib call made through it here is serialised by this mutex, so the bridge
// does not depend on XInitThreads having run before the JVM's first Xlib call.
std::mutex gDisplayMutex;

[[nodiscard]] Display* bridgeDisplay() noexcept {
    static Display* const display = ::XOpenDisplay(nullptr);
    return display;
}

} // namespace

XlibWindowHandle* createXlibAnchorWindow(int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(gDisplayMutex);
    Display* const display = bridgeDisplay();
    if (display == nullptr) {
        return nullptr;
    }
    // Never mapped: the window exists only so a Vulkan surface can be created for the
    // device. Frames are read back off-screen and drawn by the interface itself.
    const ::Window window = ::XCreateSimpleWindow(display, DefaultRootWindow(display),
            0, 0, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
            0, 0, 0);
    if (window == 0) {
        return nullptr;
    }
    ::XFlush(display);
    auto* handle = new (std::nothrow) XlibWindowHandle{};
    if (handle == nullptr) {
        ::XDestroyWindow(display, window);
        ::XFlush(display);
        return nullptr;
    }
    handle->display = display;
    handle->window = window;
    handle->width = width;
    handle->height = height;
    return handle;
}

bool resizeXlibAnchorWindow(XlibWindowHandle* window, int width, int height) noexcept {
    if (window == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(gDisplayMutex);
    auto* display = static_cast<Display*>(window->display);
    if (display == nullptr || window->window == 0) {
        return false;
    }
    ::XResizeWindow(display, static_cast<::Window>(window->window),
            static_cast<unsigned int>(width), static_cast<unsigned int>(height));
    ::XFlush(display);
    window->width = width;
    window->height = height;
    return true;
}

void destroyXlibAnchorWindow(XlibWindowHandle* window) noexcept {
    if (window == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(gDisplayMutex);
        auto* display = static_cast<Display*>(window->display);
        if (display != nullptr && window->window != 0) {
            ::XDestroyWindow(display, static_cast<::Window>(window->window));
            ::XFlush(display);
        }
    }
    delete window;
}

} // namespace cellularity::desktop
