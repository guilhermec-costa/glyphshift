#pragma once
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>

// display::Capture for X11: reads one window through X Composite (automatic
// redirection keeps it on screen and gives it a readable pixmap) and hands each
// frame to the OpenGL renderer as a texture bound from that pixmap
// (GLX_EXT_texture_from_pixmap). XDamage reports new frames. Read-only: the
// target process is never touched.
namespace glyphshift::display::x11 {
class Capture {
public:
    // Requires X Composite 0.2, XDamage and texture_from_pixmap for the
    // target's depth; throws std::runtime_error otherwise.
    Capture(Display* display, Window target);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // Starts redirection and damage reports. Call after the Tracker exists, so
    // its error handling covers the target from the first request.
    void start();

    // True when `event` reports new content (consumed: damage subtracted).
    bool handle(const XEvent& event);

    // Wraps the target's current backing pixmap; needed after every size
    // change. False when the target no longer has one.
    bool rebind();

    bool bound() const noexcept {
        return glx_pixmap_ != 0;
    }

    // Binds the current frame into, or releases it from, the GL_TEXTURE_2D
    // bound on the active texture unit. Requires a current GLX context.
    void bind() const;
    void release() const;

    // Row order of the bound image (GLX_Y_INVERTED_EXT).
    bool top_down() const noexcept {
        return top_down_;
    }

private:
    using BindTexImage = void (*)(Display*, GLXDrawable, int, const int*);
    using ReleaseTexImage = void (*)(Display*, GLXDrawable, int);

    void release_pixmap();

    Display* display_;
    Window target_;
    GLXFBConfig config_ = nullptr;
    Pixmap pixmap_ = 0;
    GLXPixmap glx_pixmap_ = 0;
    Damage damage_ = 0;
    int damage_event_ = 0;
    BindTexImage bind_ = nullptr;
    ReleaseTexImage release_ = nullptr;
    bool rgba_ = false, top_down_ = true;
};
}  // namespace glyphshift::display::x11
