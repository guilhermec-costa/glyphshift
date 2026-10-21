#pragma once
#include <windows.h>

#include <chrono>
#include <string>

// display::GlobalHotkey for Win32: the background daemon's attach/detach key
// (RegisterHotKey on a hidden window of the daemon) and the one-daemon-per-
// session claim that comes with it: a named mutex, and that window, whose
// title publishes the hotkey so a second launch can name it.
namespace glyphshift::display::win32 {
class GlobalHotkey {
public:
    // "ctrl+alt+g": ctrl, shift, alt and super joined with + to a key name (a
    // letter, a digit, F1..F24, or Insert, Delete, Home, End, Prior, Next,
    // Pause, Print, Scroll_Lock, Space, Tab, Return, Escape, Left, Right, Up,
    // Down; case does not matter). Throws std::invalid_argument otherwise.
    static void check(const std::string& hotkey);

    // Claims this session for the daemon.
    explicit GlobalHotkey(const std::string& hotkey);
    ~GlobalHotkey();
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // False when another daemon runs in this session; running() then names
    // its hotkey (empty if it could not be read).
    bool claimed() const noexcept {
        return window_ != nullptr;
    }

    const std::string& running() const noexcept {
        return other_;
    }

    // Registers the key. Throws std::runtime_error when another application
    // already holds it.
    void grab();

    // True for a press of the hotkey (a WM_HOTKEY of the daemon's window);
    // presses within 400 ms of the previous one are consumed without
    // reporting.
    bool pressed(const MSG& message);

    // The daemon's hidden window, which receives the hotkey.
    HWND window() const noexcept {
        return window_;
    }

private:
    HANDLE mutex_ = nullptr;
    HWND window_ = nullptr;
    UINT modifiers_ = 0, key_ = 0;
    bool grabbed_ = false;
    std::string other_;
    std::chrono::steady_clock::time_point last_ =
        std::chrono::steady_clock::now() - std::chrono::seconds(1);
};

// The foreground top-level window when it can carry an overlay: not the
// desktop, the taskbar or a window of GlyphShift itself; null otherwise.
HWND focused_window();

// A window's title for menus and messages, shortened.
std::string window_title(HWND window);
}  // namespace glyphshift::display::win32
