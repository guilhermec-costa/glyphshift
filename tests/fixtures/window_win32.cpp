#include <crtdbg.h>
#include <dwmapi.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "window.hpp"

// The fixture's Win32 window. Test variables, as on X11:
// GLYPHSHIFT_FIXTURE_TITLE_SUFFIX appends to the title, GLYPHSHIFT_FIXTURE_NO_FOCUS
// shows the window without activating it, GLYPHSHIFT_FIXTURE_INPUT_LOG prints
// keys (virtual-key codes) and mouse buttons on stderr.
namespace fixture {
namespace {
constexpr wchar_t class_name[] = L"GlyphShiftFixture";

std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size);
    return result;
}
}  // namespace

Window::Window() {
#ifdef _DEBUG
    // Failed debug checks report on stderr and end the fixture, rather than a
    // modal dialog that would leave an automated test waiting.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    // Physical pixels everywhere: sizes and captures match the swapchain.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW window_class{sizeof window_class};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name;
    RegisterClassExW(&window_class);  // Fails harmlessly when already registered.
}

Window::~Window() {
    close();
}

void Window::close() {
    if (handle)
        DestroyWindow(handle);
    handle = nullptr;
}

void Window::create(const char* title) {
    const char* suffix = std::getenv("GLYPHSHIFT_FIXTURE_TITLE_SUFFIX");
    const auto name = wide(std::string(title) + (suffix ? suffix : ""));
    const bool no_focus = std::getenv("GLYPHSHIFT_FIXTURE_NO_FOCUS") != nullptr;
    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    const DWORD extended = no_focus ? WS_EX_NOACTIVATE : 0;
    RECT frame{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectEx(&frame, style, FALSE, extended);
    handle = CreateWindowExW(extended,
        class_name,
        name.c_str(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        frame.right - frame.left,
        frame.bottom - frame.top,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (!handle)
        throw std::runtime_error("CreateWindowExW failed");
    // Square corners: Windows 11 rounds a window's lower corners into its
    // client area, which a window capture then lacks; tests compare captures
    // with the whole client area.
    const DWORD square = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &square, sizeof square);
    ShowWindow(handle, no_focus ? SW_SHOWNOACTIVATE : SW_SHOW);
    RECT client{};
    GetClientRect(handle, &client);
    width = static_cast<unsigned>(client.right);
    height = static_cast<unsigned>(client.bottom);
}

bool Window::poll() {
    resized = false;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !closed_;
}

LRESULT CALLBACK Window::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self)
        return DefWindowProcW(window, message, w, l);
    const bool log = std::getenv("GLYPHSHIFT_FIXTURE_INPUT_LOG") != nullptr;
    switch (message) {
        case WM_CLOSE:
            self->closed_ = true;
            return 0;
        case WM_SIZE: {
            const auto new_width = static_cast<unsigned>(LOWORD(l));
            const auto new_height = static_cast<unsigned>(HIWORD(l));
            self->resized = self->resized || new_width != self->width || new_height != self->height;
            self->width = new_width;
            self->height = new_height;
            return 0;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (log)
                std::cerr << "Fixture input: " << (message == WM_KEYDOWN ? "key down " : "key up ")
                          << w << '\n';
            if (message == WM_KEYDOWN && w == VK_ESCAPE)
                self->closed_ = true;
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            if (log)
                std::cerr << "Fixture input: "
                          << (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN
                                     ? "button down "
                                     : "button up ")
                          << (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ? 1 : 3) << '\n';
            return 0;
        case WM_PAINT:
            // Vulkan presents the contents; only validate the region.
            ValidateRect(window, nullptr);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(window, message, w, l);
}
}  // namespace fixture
