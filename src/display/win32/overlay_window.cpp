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
    window_ =
        CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
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

bool OverlayWindow::directly_above(HWND target) const {
    // The nearest visible window above the target must be the overlay.
    for (HWND above = GetWindow(target, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV))
        if (above == window_ || IsWindowVisible(above))
            return above == window_;
    return false;
}

bool OverlayWindow::show(const RECT& area, HWND target) {
    // The window's real state: a placement that did not apply is retried.
    const bool was_hidden = !IsWindowVisible(window_);
    const bool moved = area.left != area_.left || area.top != area_.top ||
                       area.right != area_.right || area.bottom != area_.bottom;
    if (was_hidden || !directly_above(target)) {
        // Into the target's band (topmost or not), then directly above it:
        // windows above the target stay above the overlay.
        const bool topmost = GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_TOPMOST;
        if (topmost != ((GetWindowLongPtrW(window_, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0))
            SetWindowPos(window_,
                topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        HWND after = GetWindow(target, GW_HWNDPREV);
        if (after == window_)
            after = GetWindow(window_, GW_HWNDPREV);
        // A window of the other band right above the target (the taskbar over
        // the top normal window) means the target leads its band.
        if (after && ((GetWindowLongPtrW(after, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost)
            after = nullptr;
        SetWindowPos(window_,
            after ? after : (topmost ? HWND_TOPMOST : HWND_TOP),
            area.left,
            area.top,
            area.right - area.left,
            area.bottom - area.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (moved) {
        SetWindowPos(window_,
            nullptr,
            area.left,
            area.top,
            area.right - area.left,
            area.bottom - area.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }
    area_ = area;
    shown_ = true;
    return was_hidden;
}

void OverlayWindow::hide() {
    if (IsWindowVisible(window_))
        ShowWindow(window_, SW_HIDE);
    shown_ = false;
}
}  // namespace glyphshift::display::win32
