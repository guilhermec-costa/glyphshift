#pragma once
#include <X11/Xlib.h>

#include <string>

namespace glyphshift::display::x11 {
// display::Tracker for X11. Follows one window chosen by the user: its
// on-screen geometry, whether it is viewable, and where it sits in the stacking
// order. It only reads state and selects notification events; it never changes
// the target. X errors about the target are ignored process-wide from here on:
// it can disappear between any two requests, and the next query reports that.
class Tracker {
public:
    struct State {
        int x = 0, y = 0;
        unsigned width = 0, height = 0;
        bool viewable = false;
        bool exists = false;
    };

    Tracker(Display* display, Window target);

    Window target() const noexcept {
        return target_;
    }

    // The target's top-level ancestor (a window-manager frame, or the target).
    Window frame() const noexcept {
        return frame_;
    }

    // Current geometry in root coordinates and viewability.
    State query() const;

    // The application's class name (WM_CLASS class, else instance), or empty.
    std::string application_class() const;

    // True when an event can change the target's geometry, visibility or
    // stacking. Events about `ignored` (the overlay itself) never count.
    bool affects(const XEvent& event, Window ignored) const noexcept;

    // True once the target or its frame has been destroyed.
    bool destroyed(const XEvent& event) const noexcept;

    // Stacks `window` (a root child) directly above the target's frame, so
    // windows raised above the target also stay above `window`. Returns false
    // when the stacking was already correct.
    bool stack_above(Window window) const;

private:
    Display* display_;
    Window target_, frame_ = 0, root_;
};
}  // namespace glyphshift::display::x11
