#include "editor_input.hpp"

#include <windowsx.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>

#include "keys.hpp"

namespace glyphshift::display::win32 {
namespace {
using editor::Input;
using editor::Kind;

constexpr wchar_t property[] = L"GlyphShiftEditorInput";
constexpr LONG_PTR transparent = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;

struct Session {
    HWND overlay = nullptr, target = nullptr;
    WNDPROC previous = nullptr;
    bool no_focus = false;
    bool registered = false, conflict = false;
    // The overlay takes input (not transparent, activatable).
    bool interactive = false;
    // The foreground was lost while the editor was open.
    bool deactivated = false;
    std::deque<Input> queue;
    // Keys (scan codes) and buttons the editor received and still holds.
    std::array<bool, 512> keys{};
    unsigned buttons = 0;
    bool control = false, alt = false, super = false;

    bool held() const {
        bool any = buttons != 0;
        for (const bool key : keys)
            any |= key;
        return any;
    }

    void error(const char* message) {
        std::fprintf(stderr, "glyphshift: editor input: %s\n", message);
        editor::report(message);
    }

    // Insert and F10 belong to the overlay while the target or the open
    // editor is in the foreground.
    void hotkeys() {
        if (no_focus)
            return;
        const HWND foreground = GetForegroundWindow();
        const bool want = foreground == target || foreground == overlay;
        if (want == registered)
            return;
        if (want) {
            const bool insert = RegisterHotKey(overlay, insert_hotkey, MOD_NOREPEAT, vk::insert);
            const bool f10 = RegisterHotKey(overlay, f10_hotkey, MOD_NOREPEAT, vk::f10);
            if (!insert || !f10) {
                if (insert)
                    UnregisterHotKey(overlay, insert_hotkey);
                if (f10)
                    UnregisterHotKey(overlay, f10_hotkey);
                if (!conflict)
                    error("Insert or F10 is registered as a hotkey by another application");
                conflict = true;
                return;
            }
            registered = true;
        } else {
            UnregisterHotKey(overlay, insert_hotkey);
            UnregisterHotKey(overlay, f10_hotkey);
            registered = false;
        }
    }

    void set_interactive(bool value) {
        const LONG_PTR style = GetWindowLongPtrW(overlay, GWL_EXSTYLE);
        SetWindowLongPtrW(overlay, GWL_EXSTYLE, value ? style & ~transparent : style | transparent);
        interactive = value;
        if (value) {
            deactivated = false;
            if (!no_focus)
                SetForegroundWindow(overlay);
        } else {
            keys.fill(false);
            buttons = 0;
            if (GetCapture() == overlay)
                ReleaseCapture();
            // Return the keyboard to the game, never taking it from another
            // application the user switched to.
            if (!no_focus && GetForegroundWindow() == overlay)
                SetForegroundWindow(target);
        }
    }

    void key(UINT message, WPARAM w, LPARAM l) {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const auto scan = static_cast<unsigned>((l >> 16) & 0x1ff);
        const auto virtual_key = static_cast<unsigned>(w);
        // The key's text in the current layout, without changing dead-key state.
        std::array<BYTE, 256> state{};
        GetKeyboardState(state.data());
        std::array<wchar_t, 4> text{};
        const int count = ToUnicode(virtual_key, scan, state.data(), text.data(), 4, 4);
        char32_t character = 0;
        if (count == 1)
            character = text[0];
        else if (count == 2 && text[0] >= 0xd800 && text[0] < 0xdc00)
            character = 0x10000 + ((char32_t{text[0]} - 0xd800) << 10) + (text[1] - 0xdc00);
        Input event{down ? Kind::key_down : Kind::key_up};
        translate_virtual_key(virtual_key, character, event);
        if (event.key == editor::Key::control)
            control = down;
        if (event.key == editor::Key::alt)
            alt = down;
        if (event.key == editor::Key::super)
            super = down;
        event.physical = scan ? scan : virtual_key + 0x200;
        event.time = static_cast<unsigned long>(GetMessageTime());
        event.modified = control || alt || super || (GetKeyState(VK_CONTROL) & 0x8000) ||
                         (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
        keys[scan] = down;
        queue.push_back(event);
    }

    void pointer(Kind kind, unsigned button, LPARAM l) {
        Input event{kind};
        event.button = button;
        event.x = static_cast<float>(GET_X_LPARAM(l));
        event.y = static_cast<float>(GET_Y_LPARAM(l));
        event.time = static_cast<unsigned long>(GetMessageTime());
        if (kind == Kind::button_down && button < 32) {
            buttons |= 1U << button;
            SetCapture(overlay);
        }
        if (kind == Kind::button_up && button < 32) {
            buttons &= ~(1U << button);
            if (!buttons && GetCapture() == overlay)
                ReleaseCapture();
        }
        queue.push_back(event);
    }

    void hotkey(int id) {
        // A hotkey delivers its press only; the editor also expects the release.
        Input event{Kind::key_down};
        event.key = id == insert_hotkey ? editor::Key::insert : editor::Key::f10;
        event.physical = id == insert_hotkey ? 0x152 : 0x44;  // Their scan codes.
        event.time = static_cast<unsigned long>(GetMessageTime());
        queue.push_back(event);
        event.kind = Kind::key_up;
        queue.push_back(event);
    }
};

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* session = static_cast<Session*>(GetPropW(window, property));
    if (!session)
        return DefWindowProcW(window, message, w, l);
    switch (message) {
        case WM_HOTKEY:
            session->hotkey(static_cast<int>(w));
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            session->key(message, w, l);
            return 0;
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_DEADCHAR:
            return 0;
        case WM_MOUSEMOVE:
            session->pointer(Kind::motion, 0, l);
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            session->pointer(message == WM_LBUTTONDOWN ? Kind::button_down : Kind::button_up, 1, l);
            return 0;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            session->pointer(message == WM_MBUTTONDOWN ? Kind::button_down : Kind::button_up, 2, l);
            return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            session->pointer(message == WM_RBUTTONDOWN ? Kind::button_down : Kind::button_up, 3, l);
            return 0;
        case WM_MOUSEWHEEL: {
            // Screen coordinates here; the editor wants client ones.
            POINT at{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(window, &at);
            const LPARAM client = MAKELPARAM(at.x, at.y);
            const unsigned button = GET_WHEEL_DELTA_WPARAM(w) > 0 ? 4 : 5;
            session->pointer(Kind::button_down, button, client);
            session->pointer(Kind::button_up, button, client);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return session->interactive ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_SETCURSOR:
            if (session->interactive) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE && session->interactive && !session->no_focus)
                session->deactivated = true;
            break;
        default:
            break;
    }
    return CallWindowProcW(session->previous, window, message, w, l);
}

std::map<std::uint64_t, std::unique_ptr<Session>>& sessions() {
    static std::map<std::uint64_t, std::unique_ptr<Session>> value;
    return value;
}

void sync(void* display, std::uint64_t window, bool& open);

void poll(void* display, std::uint64_t window, bool& open) {
    const auto it = sessions().find(window);
    if (it == sessions().end())
        return;
    auto& session = *it->second;
    session.hotkeys();
    while (!session.queue.empty()) {
        const auto event = session.queue.front();
        session.queue.pop_front();
        editor::input(display, window, event);
    }
    if (session.deactivated) {
        session.deactivated = false;
        editor::input(display, window, {Kind::focus_lost});
        open = false;
    }
    sync(display, window, open);
}

void sync(void* display, std::uint64_t window, bool& open) {
    const auto it = sessions().find(window);
    if (it == sessions().end())
        return;
    auto& session = *it->second;
    if (open) {
        if (!session.interactive)
            session.set_interactive(true);
        // Without the foreground the keyboard still goes to the game: close.
        if (!session.no_focus && GetForegroundWindow() != session.overlay) {
            open = false;
            editor::input(display, window, {Kind::focus_lost});
        }
    }
    // Keep input until the editor's own keys and buttons are released, so an
    // Escape or a click that closed it does not reach the game.
    if (!open && session.interactive && !session.held())
        session.set_interactive(false);
    session.hotkeys();
}

void forget(void*, std::uint64_t window) {
    const auto it = sessions().find(window);
    if (it == sessions().end())
        return;
    auto& session = *it->second;
    if (session.interactive)
        session.set_interactive(false);
    if (session.registered) {
        UnregisterHotKey(session.overlay, insert_hotkey);
        UnregisterHotKey(session.overlay, f10_hotkey);
    }
    if (IsWindow(session.overlay)) {
        SetWindowLongPtrW(session.overlay,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(session.previous));
        RemovePropW(session.overlay, property);
    }
    sessions().erase(it);
}

constexpr editor::InputSource source{poll, sync, forget};
}  // namespace

const editor::InputSource* editor_input(HWND overlay, HWND target) {
    const auto key = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));
    auto& slot = sessions()[key];
    if (!slot) {
        slot = std::make_unique<Session>();
        slot->overlay = overlay;
        slot->target = target;
        const char* no_focus = std::getenv("GLYPHSHIFT_OVERLAY_NO_FOCUS");
        slot->no_focus = no_focus && *no_focus == '1';
        SetPropW(overlay, property, slot.get());
        slot->previous = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(overlay, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure)));
    }
    return &source;
}
}  // namespace glyphshift::display::win32
