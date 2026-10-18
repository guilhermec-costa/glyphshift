#pragma once
#include <X11/Xlib.h>

#include <chrono>
#include <string>

// display::GlobalHotkey for X11: the background daemon's attach/detach key, a
// passive grab on the root window (with and without CapsLock and NumLock), and
// the one-daemon-per-display claim that comes with it.
namespace glyphshift::display::x11 {
class GlobalHotkey {
public:
    // "ctrl+alt+g": ctrl, shift, alt and super joined with + to a keysym name.
    // Throws std::invalid_argument when `hotkey` cannot be parsed.
    static void check(const std::string& hotkey);

    // Claims the display for this daemon. Like window managers and compositors,
    // it owns a selection, held by a hidden window that publishes the hotkey;
    // the X server releases it when the process exits. Throws
    // std::invalid_argument when the key is not on this keyboard.
    GlobalHotkey(Display* display, const std::string& hotkey);
    ~GlobalHotkey();
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // False when another daemon runs on this display; running() then names its
    // hotkey (empty if it published none).
    bool claimed() const noexcept {
        return owner_ != 0;
    }

    const std::string& running() const noexcept {
        return other_;
    }

    // Grabs the key on the root window. Throws std::runtime_error when another
    // application already holds it.
    void grab();

    // True for a press of the hotkey; auto-repeat and double presses within
    // 400 ms are consumed without reporting.
    bool pressed(const XEvent& event);

    // True when `event` is a key press on the root window (the grab's events).
    bool owns(const XEvent& event) const noexcept;

private:
    Display* display_;
    Window root_, owner_ = 0;
    unsigned modifiers_ = 0;
    KeyCode code_ = 0;
    bool grabbed_ = false;
    std::string other_;
    std::chrono::steady_clock::time_point last_ =
        std::chrono::steady_clock::now() - std::chrono::seconds(1);
};

// The keyboard focus resolved to its managed client (the ancestor with
// WM_STATE, so Wine's inner presentation windows map to the game's client),
// else _NET_ACTIVE_WINDOW (EWMH); 0 when there is none.
Window focused_client(Display* display);

// A window's title for menus: _NET_WM_NAME (UTF-8), else WM_NAME, shortened.
std::string window_title(Display* display, Window window);
}  // namespace glyphshift::display::x11
