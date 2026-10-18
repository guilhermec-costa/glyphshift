#include "overlay_window.hpp"

#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <stdexcept>

namespace glyphshift::display::x11 {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

using SwapInterval = void (*)(Display*, GLXDrawable, int);
using CreateContext = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
}  // namespace

OverlayWindow::OverlayWindow(Display* display, unsigned width, unsigned height)
    : display_(display) {
    int event = 0, error = 0;
    require(XFixesQueryExtension(display_, &event, &error), "XFixes is required");
    const int screen = DefaultScreen(display_);
    const int attributes[] = {GLX_X_RENDERABLE,
        True,
        GLX_DRAWABLE_TYPE,
        GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,
        GLX_RGBA_BIT,
        GLX_DOUBLEBUFFER,
        True,
        GLX_RED_SIZE,
        8,
        GLX_GREEN_SIZE,
        8,
        GLX_BLUE_SIZE,
        8,
        None};
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display_, screen, attributes, &count);
    require(configs && count, "no double-buffered GLX window config");
    config_ = configs[0];
    XFree(configs);
    XVisualInfo* visual = glXGetVisualFromFBConfig(display_, config_);
    require(visual, "no visual for GLX window config");
    XSetWindowAttributes values{};
    values.override_redirect = True;
    values.colormap =
        XCreateColormap(display_, RootWindow(display_, screen), visual->visual, AllocNone);
    values.border_pixel = 0;
    values.background_pixmap = None;
    window_ = XCreateWindow(display_,
        RootWindow(display_, screen),
        0,
        0,
        width,
        height,
        0,
        visual->depth,
        InputOutput,
        visual->visual,
        CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixmap,
        &values);
    XFree(visual);
    require(window_, "cannot create overlay window");
    XStoreName(display_, window_, "GlyphShift overlay");
    // Empty input shape: every click and key goes to the window below.
    const XserverRegion empty = XFixesCreateRegion(display_, nullptr, 0);
    XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, empty);
    XFixesDestroyRegion(display_, empty);
    // Ask compositors (picom/compton) not to shadow or fade the overlay.
    const unsigned long no_shadow = 0;
    XChangeProperty(display_,
        window_,
        XInternAtom(display_, "_COMPTON_SHADOW", False),
        XA_CARDINAL,
        32,
        PropModeReplace,
        reinterpret_cast<const unsigned char*>(&no_shadow),
        1);

    const auto create = reinterpret_cast<CreateContext>(
        glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    require(create, "GLX_ARB_create_context is required");
    const int context_attributes[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
        3,
        GLX_CONTEXT_MINOR_VERSION_ARB,
        3,
        GLX_CONTEXT_PROFILE_MASK_ARB,
        GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None};
    context_ = create(display_, config_, nullptr, True, context_attributes);
    require(context_ && glXMakeCurrent(display_, window_, context_),
        "cannot create OpenGL 3.3 core context");
    // Present on vblank to avoid tearing; damage drives when frames start.
    const auto interval = reinterpret_cast<SwapInterval>(
        glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT")));
    if (interval)
        interval(display_, window_, 1);
}

OverlayWindow::~OverlayWindow() {
    if (context_) {
        glXMakeCurrent(display_, None, nullptr);
        glXDestroyContext(display_, context_);
    }
    if (window_)
        XDestroyWindow(display_, window_);
}

bool OverlayWindow::show(const Tracker::State& state, bool moved, const Tracker& tracker) {
    if (moved || !shown_)
        XMoveResizeWindow(display_, window_, state.x, state.y, state.width, state.height);
    const bool appeared = !shown_;
    if (!shown_) {
        XMapWindow(display_, window_);
        shown_ = true;
    }
    tracker.stack_above(window_);
    return appeared;
}

void OverlayWindow::hide() {
    if (shown_) {
        XUnmapWindow(display_, window_);
        shown_ = false;
    }
}

void OverlayWindow::make_current() const {
    if (context_)
        glXMakeCurrent(display_, window_, context_);
}

void OverlayWindow::present() const {
    glXSwapBuffers(display_, window_);
}
}  // namespace glyphshift::display::x11
