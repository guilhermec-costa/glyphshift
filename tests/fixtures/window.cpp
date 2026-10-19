#include "window.hpp"

#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <dlfcn.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace fixture {
Window::Window() : display(XOpenDisplay(nullptr)) {
    if (!display)
        throw std::runtime_error("Cannot open X11 display; check DISPLAY and Xauthority");
}

Window::~Window() {
    close();
}

void Window::close() {
    if (!display)
        return;
    if (handle)
        XDestroyWindow(display, handle);
    if (colormap_)
        XFreeColormap(display, colormap_);
    XCloseDisplay(display);
    handle = 0;
    colormap_ = 0;
    display = nullptr;
}

void Window::create(XVisualInfo* visual, const char* title) {
    const auto root = RootWindow(display, visual->screen);
    colormap_ = XCreateColormap(display, root, visual->visual, AllocNone);
    XSetWindowAttributes attrs{};
    attrs.colormap = colormap_;
    attrs.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask | ExposureMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask;
    if (const char* mode = std::getenv("GLYPHSHIFT_FIXTURE_INPUT_MODE"))
        attrs.event_mask = StructureNotifyMask | ExposureMask |
                           (std::string_view(mode) == "press-only" ? KeyPressMask : KeyReleaseMask);
    handle = XCreateWindow(display,
        root,
        0,
        0,
        width,
        height,
        0,
        visual->depth,
        InputOutput,
        visual->visual,
        CWColormap | CWEventMask,
        &attrs);
    if (!handle)
        throw std::runtime_error("XCreateWindow failed");
    const char* suffix = std::getenv("GLYPHSHIFT_FIXTURE_TITLE_SUFFIX");
    const std::string name = std::string(title) + (suffix ? suffix : "");
    XStoreName(display, handle, name.c_str());
    if (std::getenv("GLYPHSHIFT_FIXTURE_FLOATING")) {
        const auto type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        const auto dialog = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        XChangeProperty(display,
            handle,
            type,
            XA_ATOM,
            32,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(&dialog),
            1);
    }
    close_ = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, handle, &close_, 1);
    // Automated input checks send events directly; leave desktop keyboard
    // focus with the user's application during those runs.
    if (std::getenv("GLYPHSHIFT_FIXTURE_NO_FOCUS")) {
        XWMHints hints{};
        hints.flags = InputHint;
        hints.input = False;
        XSetWMHints(display, handle, &hints);
    }
    XMapWindow(display, handle);
    XFlush(display);
}

bool Window::poll() {
    resized = false;
    // Regression target for applications such as vkcube/Wine that fetch Xlib
    // functions privately and bypass LD_PRELOAD event interposition.
    static const auto library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    static const auto pending =
        reinterpret_cast<decltype(&XPending)>(library ? dlsym(library, "XPending") : nullptr);
    static const auto next =
        reinterpret_cast<decltype(&XNextEvent)>(library ? dlsym(library, "XNextEvent") : nullptr);
    const bool private_input = std::getenv("GLYPHSHIFT_FIXTURE_PRIVATE_XLIB") && pending && next;
    while ((private_input ? pending : &XPending)(display)) {
        XEvent event{};
        (private_input ? next : &XNextEvent)(display, &event);
        if (event.type == ClientMessage &&
            event.xclient.message_type == XInternAtom(display, "WM_PROTOCOLS", False) &&
            static_cast<Atom>(event.xclient.data.l[0]) == close_)
            return false;
        if (std::getenv("GLYPHSHIFT_FIXTURE_INPUT_LOG")) {
            if (event.type == KeyPress || event.type == KeyRelease)
                std::cerr << "Fixture input: " << (event.type == KeyPress ? "key down " : "key up ")
                          << XLookupKeysym(&event.xkey, 0) << '\n';
            if (event.type == ButtonPress || event.type == ButtonRelease)
                std::cerr << "Fixture input: "
                          << (event.type == ButtonPress ? "button down " : "button up ")
                          << event.xbutton.button << '\n';
        }
        if (event.type == KeyPress && XLookupKeysym(&event.xkey, 0) == XK_Escape)
            return false;
        if (event.type == ConfigureNotify) {
            const auto w = static_cast<unsigned>(event.xconfigure.width);
            const auto h = static_cast<unsigned>(event.xconfigure.height);
            resized = resized || w != width || h != height;
            width = w;
            height = h;
        }
    }
    return true;
}

}  // namespace fixture
