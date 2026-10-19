#pragma once
#include <windows.h>

// display::Overlay for Win32: a layered, input-transparent popup
// (WS_EX_LAYERED | WS_EX_TRANSPARENT: hit tests and input fall through to the
// window below), never activated and absent from the taskbar, kept above the
// target. The renderer presents into it through its own Vulkan surface.
namespace glyphshift::display::win32 {
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    HWND handle() const noexcept {
        return window_;
    }

    bool shown() const noexcept {
        return shown_;
    }

    // Shows the window over `area` (screen pixels). Returns true when it was
    // hidden before.
    bool show(const RECT& area);
    void hide();

private:
    HWND window_ = nullptr;
    RECT area_{};
    bool shown_ = false;
};
}  // namespace glyphshift::display::win32
