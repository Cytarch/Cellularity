#pragma once

// A real Win32 top-level window that is never shown.
//
// The Windows Vulkan contract tests need a window a surface can be created for,
// but they must not need an interactive desktop session or leave anything visible
// on screen. A window that is created and never shown satisfies both: the driver
// accepts it for presentation, and nothing is rendered to the user's desktop.
//
// The window is destroyed with the object, so a test that throws still cleans up.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdexcept>
#include <string>

namespace cellularity::tests {

class OffscreenWin32Window final {
public:
    OffscreenWin32Window() {
        const HINSTANCE instance = ::GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &OffscreenWin32Window::windowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"CellularityVulkanContractWindow";
        // Registering the same class twice fails with ERROR_CLASS_ALREADY_EXISTS,
        // which is expected when a second window is created in one process.
        ::RegisterClassExW(&windowClass);

        mWindow = ::CreateWindowExW(0, windowClass.lpszClassName,
                L"Cellularity Vulkan contract", WS_OVERLAPPEDWINDOW, 0, 0, 640,
                480, nullptr, nullptr, instance, nullptr);
        if (mWindow == nullptr) {
            throw std::runtime_error(
                    "Could not create the contract test window, error "
                    + std::to_string(::GetLastError()));
        }
    }

    ~OffscreenWin32Window() {
        if (mWindow != nullptr) {
            ::DestroyWindow(mWindow);
        }
    }

    OffscreenWin32Window(const OffscreenWin32Window&) = delete;
    OffscreenWin32Window& operator=(const OffscreenWin32Window&) = delete;

    [[nodiscard]] HWND handle() const noexcept {
        return mWindow;
    }

private:
    static LRESULT CALLBACK windowProc(
            HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    HWND mWindow = nullptr;
};

} // namespace cellularity::tests
