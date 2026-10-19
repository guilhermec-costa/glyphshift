#include "overlay_window.hpp"

#include <stdexcept>

namespace glyphshift::display::win32 {
namespace {
constexpr wchar_t class_name[] = L"GlyphShiftOverlay";

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_PAINT:
            // Vulkan presents the contents.
            ValidateRect(window, nullptr);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProcW(window, message, w, l);
    }
}
}  // namespace

OverlayWindow::OverlayWindow() {
    WNDCLASSEXW window_class{sizeof window_class};
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    RegisterClassExW(&window_class);  // Fails harmlessly when already registered.
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        class_name,
        L"GlyphShift overlay",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        window_class.hInstance,
        nullptr);
    if (!window_)
        throw std::runtime_error("cannot create the overlay window");
    // Fully opaque: layering is only for the input transparency.
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
}

OverlayWindow::~OverlayWindow() {
    if (window_)
        DestroyWindow(window_);
}

bool OverlayWindow::show(const RECT& area) {
    const bool was_hidden = !shown_;
    const bool moved = area.left != area_.left || area.top != area_.top ||
                       area.right != area_.right || area.bottom != area_.bottom;
    if (moved || was_hidden)
        SetWindowPos(window_,
            HWND_TOPMOST,
            area.left,
            area.top,
            area.right - area.left,
            area.bottom - area.top,
            SWP_NOACTIVATE | (was_hidden ? SWP_SHOWWINDOW : 0));
    area_ = area;
    shown_ = true;
    return was_hidden;
}

void OverlayWindow::hide() {
    if (shown_)
        ShowWindow(window_, SW_HIDE);
    shown_ = false;
}
}  // namespace glyphshift::display::win32
