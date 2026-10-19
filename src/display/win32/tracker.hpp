#pragma once
#include <windows.h>

#include <string>

// display::Tracker for Win32: the target window's state, read without
// affecting it.
namespace glyphshift::display::win32 {
class Tracker {
public:
    struct State {
        bool exists = false;
        // Shown, not minimized and not cloaked (another virtual desktop).
        bool viewable = false;
        // The client area in screen pixels.
        RECT client{};
    };

    explicit Tracker(HWND target) : target_(target) {}

    State query() const;

    // The target's executable name without extension ("supertux2"), for the
    // per-game profile; empty when it cannot be read.
    std::string application() const;

private:
    HWND target_;
};
}  // namespace glyphshift::display::win32
