#include "tray.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "../../platform/windows/text.hpp"

namespace glyphshift::display::win32 {
namespace {
constexpr wchar_t tray_class[] = L"GlyphShiftTray";
constexpr UINT icon_id = 1;
// The icon's callback, and the request to open the menu outside it.
constexpr UINT callback = WM_APP + 1, open_menu = WM_APP + 2;
// The application icon (data/glyphshift.svg): a "G" of glyph cells.
constexpr const char* glyph_rows[] =
    {".####.", "#....#", "#.....", "#..###", "#....#", "#....#", ".####."};

using platform::windows::wide;

// A square icon of `size` pixels: the "G" in the accent color on the dark
// rounded tile of the application icon.
HICON make_icon(int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // Top-down rows.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return nullptr;
    auto* pixels = static_cast<std::uint32_t*>(bits);
    const int radius = size / 5;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // Distance into a corner's square, measured from the inner corner.
            const int dx = std::max({radius - x, x - (size - 1 - radius), 0});
            const int dy = std::max({radius - y, y - (size - 1 - radius), 0});
            pixels[y * size + x] = dx * dx + dy * dy <= radius * radius ? 0xff151824U : 0U;
        }
    const int cell = std::max(2, size * 4 / 5 / 7);
    const int left = (size - cell * 6) / 2, top = (size - cell * 7) / 2;
    const int gap = cell >= 4 ? 1 : 0;
    for (int row = 0; row < 7; ++row)
        for (int column = 0; column < 6; ++column)
            if (glyph_rows[row][column] == '#')
                for (int y = 0; y < cell - gap; ++y)
                    for (int x = 0; x < cell - gap; ++x)
                        pixels[(top + row * cell + y) * size + left + column * cell + x] =
                            0xffffd166U;
    // The alpha channel decides; the mask is ignored for 32-bit colors.
    const HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO icon{TRUE, 0, 0, mask, color};
    const HICON result = CreateIconIndirect(&icon);
    DeleteObject(mask);
    DeleteObject(color);
    return result;
}

// Menus take & as a mnemonic marker: keep titles' ampersands literal.
std::wstring menu_label(const std::string& label) {
    std::wstring result;
    for (const wchar_t c : wide(label)) {
        result += c;
        if (c == L'&')
            result += L'&';
    }
    return result;
}

template <std::size_t N>
void copy(wchar_t (&target)[N], const std::string& text) {
    wcsncpy_s(target, wide(text).c_str(), _TRUNCATE);
}
}  // namespace

struct Tray::State {
    HWND window = nullptr;
    HICON icon = nullptr;
    std::string tip;
    bool take_foreground = true, shown = false, balloon = false, pending = false;
    UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    POINT anchor{};

    NOTIFYICONDATAW data(UINT flags) const {
        NOTIFYICONDATAW result{sizeof result};
        result.hWnd = window;
        result.uID = icon_id;
        result.uFlags = flags;
        return result;
    }

    // Adds the icon, or replaces it when the taskbar still has it; the size
    // follows the current DPI.
    void add() {
        if (icon)
            DestroyIcon(icon);
        // The program's application icon (resource 1) at this size, else the
        // same design drawn here.
        const int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(window));
        icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(1),
            IMAGE_ICON,
            size,
            size,
            LR_DEFAULTCOLOR));
        if (!icon)
            icon = make_icon(size);
        auto icon_data = data(NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP);
        icon_data.uCallbackMessage = callback;
        icon_data.hIcon = icon;
        copy(icon_data.szTip, tip);
        shown = Shell_NotifyIconW(NIM_ADD, &icon_data) || Shell_NotifyIconW(NIM_MODIFY, &icon_data);
        if (shown) {
            icon_data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &icon_data);
        }
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state && message == state->taskbar_created) {
            state->add();
            return 0;
        }
        if (!state || message != callback)
            return DefWindowProcW(window, message, w, l);
        switch (LOWORD(l)) {
            case NIN_SELECT:
            case NIN_KEYSELECT:
            case WM_CONTEXTMENU:
                // Any click opens the menu. It is opened from the caller's
                // loop, not inside this callback; a keyboard selection may
                // arrive twice.
                state->anchor = {GET_X_LPARAM(w), GET_Y_LPARAM(w)};
                if (!state->pending)
                    PostMessageW(window, open_menu, 0, 0);
                state->pending = true;
                break;
            case NIN_BALLOONHIDE:
            case NIN_BALLOONTIMEOUT:
            case NIN_BALLOONUSERCLICK:
                state->balloon = false;
                break;
            default:
                break;
        }
        return 0;
    }
};

Tray::Tray(const std::string& tip, bool take_foreground) : state_(std::make_unique<State>()) {
    state_->tip = tip;
    state_->take_foreground = take_foreground;
    WNDCLASSEXW window_class{sizeof window_class};
    window_class.lpfnWndProc = State::procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = tray_class;
    RegisterClassExW(&window_class);  // Fails harmlessly when already registered.
    // Never shown; top-level, so it receives the TaskbarCreated broadcast.
    state_->window = CreateWindowExW(WS_EX_TOOLWINDOW,
        tray_class,
        L"GlyphShift",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        window_class.hInstance,
        nullptr);
    if (!state_->window)
        throw std::runtime_error("cannot create the notification-area window");
    SetWindowLongPtrW(state_->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state_.get()));
    state_->add();
}

Tray::~Tray() {
    auto icon_data = state_->data(0);
    Shell_NotifyIconW(NIM_DELETE, &icon_data);
    SetWindowLongPtrW(state_->window, GWLP_USERDATA, 0);
    DestroyWindow(state_->window);
    if (state_->icon)
        DestroyIcon(state_->icon);
}

int Tray::handle(const MSG& message, const std::function<std::vector<MenuItem>()>& menu) {
    if (message.hwnd != state_->window || message.message != open_menu)
        return -1;
    state_->pending = false;
    const HMENU popup = CreatePopupMenu();
    for (const auto& row : menu()) {
        if (row.separator) {
            AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (row.id < 0)
            flags |= MF_GRAYED;
        if (row.checkable && row.checked)
            flags |= MF_CHECKED;
        AppendMenuW(popup,
            flags,
            static_cast<UINT_PTR>(std::max(row.id, 0)),
            menu_label(row.label).c_str());
    }
    // The documented pattern for notification-area menus: the owner in the
    // foreground while the menu is open, then a message so it closes properly.
    if (state_->take_foreground)
        SetForegroundWindow(state_->window);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const int chosen = static_cast<int>(TrackPopupMenuEx(popup,
        align | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        state_->anchor.x,
        state_->anchor.y,
        state_->window,
        nullptr));
    PostMessageW(state_->window, WM_NULL, 0, 0);
    DestroyMenu(popup);
    return chosen > 0 ? chosen : -1;
}

bool Tray::notify(const std::string& summary, const std::string& body) {
    if (!state_->shown)
        return false;
    auto icon_data = state_->data(NIF_INFO);
    copy(icon_data.szInfoTitle, summary);
    copy(icon_data.szInfo, body);
    icon_data.dwInfoFlags = NIIF_INFO;
    state_->balloon = Shell_NotifyIconW(NIM_MODIFY, &icon_data) != FALSE;
    return state_->balloon;
}

bool Tray::notifying() const {
    return state_->balloon;
}

HWND Tray::window() const {
    return state_->window;
}

bool Tray::shown() const {
    return state_->shown;
}
}  // namespace glyphshift::display::win32
