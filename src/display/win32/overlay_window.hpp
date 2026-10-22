#pragma once
#include <windows.h>

// display::Overlay for Win32: a layered, input-transparent popup
// (WS_EX_LAYERED | WS_EX_TRANSPARENT: hit tests and input fall through to the
// window below), never activated and absent from the taskbar. While the
// target (or the open editor) is the foreground window the overlay is topmost,
// so a fullscreen game that raises itself cannot cover it; otherwise it is kept
// directly above the target in the target's band, so windows above the target
// stay above it. The renderer presents into it through its own Vulkan surface.
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

    // Shows the window over `area` (screen pixels), directly above `target`
    // (restacked when another window came between them). Returns true when it
    // was hidden before.
    bool show(const RECT& area, HWND target);

    // Whether the nearest visible window above `target` is the overlay.
    bool directly_above(HWND target) const;
    // Whether the overlay is anywhere above `target`.
    bool above(HWND target) const;
    void hide();

private:
    HWND window_ = nullptr;
    RECT area_{};
    bool shown_ = false;
};
}  // namespace glyphshift::display::win32
