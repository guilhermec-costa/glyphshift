#pragma once
#include <windows.h>

#include <string>

// display::Tracker for Win32: the target window's state, read without
// affecting it. Out-of-context window-event hooks (nothing is loaded into the
// target) post the target's moves, resizes, minimizing, showing, hiding and
// destruction, and foreground changes, to this thread's message queue, so a
// message wait wakes as they happen; query() then reads the new state.
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

    explicit Tracker(HWND target);
    ~Tracker();
    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;

    State query() const;

    // The target's executable name without extension ("supertux2"), for the
    // per-game profile; empty when it cannot be read.
    std::string application() const;

private:
    HWND target_;
    HWINEVENTHOOK hooks_[3]{};
};
}  // namespace glyphshift::display::win32
