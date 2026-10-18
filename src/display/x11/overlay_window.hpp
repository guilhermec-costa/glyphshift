#pragma once
#include <GL/glx.h>
#include <X11/Xlib.h>

#include "tracker.hpp"

// display::Overlay for X11: an override-redirect window with an empty input
// shape, so every click and key reaches the window below, and its own OpenGL
// 3.3 core context presenting on vblank. The Tracker keeps it directly above
// the target in the stacking order.
namespace glyphshift::display::x11 {
class OverlayWindow {
public:
    // Requires XFixes and GLX_ARB_create_context; throws std::runtime_error
    // otherwise. The new context is current on return.
    OverlayWindow(Display* display, unsigned width, unsigned height);
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    Window window() const noexcept {
        return window_;
    }

    bool shown() const noexcept {
        return shown_;
    }

    // Shows the window at `state`'s geometry (moving it when `moved`) directly
    // above the tracked target. Returns true when it was hidden before.
    bool show(const Tracker::State& state, bool moved, const Tracker& tracker);
    void hide();

    // Makes the context current again, for releasing GL resources.
    void make_current() const;
    void present() const;

private:
    Display* display_;
    Window window_ = 0;
    GLXFBConfig config_ = nullptr;
    GLXContext context_ = nullptr;
    bool shown_ = false;
};
}  // namespace glyphshift::display::x11
