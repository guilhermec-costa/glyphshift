#include "tracker.hpp"

#include <stdexcept>

namespace glyphshift::overlay {
namespace {
struct Children {
    Window* windows = nullptr;
    unsigned count = 0;

    ~Children() {
        if (windows)
            XFree(windows);
    }
};
}  // namespace

Tracker::Tracker(Display* display, Window target)
    : display_(display), target_(target), root_(DefaultRootWindow(display)) {
    // Walk up to the root's child: reparenting window managers wrap clients in
    // frames, and stacking happens among root children.
    Window current = target;
    while (true) {
        Window root = 0, parent = 0;
        Children children;
        if (!XQueryTree(display_, current, &root, &parent, &children.windows, &children.count))
            throw std::runtime_error("target window not found");
        if (!parent || parent == root_)
            break;
        current = parent;
    }
    frame_ = current;
    XSelectInput(display_, target_, StructureNotifyMask);
    if (frame_ != target_)
        XSelectInput(display_, frame_, StructureNotifyMask);
    // Root substructure notifications report restacking among top-levels.
    XSelectInput(display_, root_, SubstructureNotifyMask);
}

Tracker::State Tracker::query() const {
    State state;
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display_, target_, &attributes))
        return state;
    Window child = 0;
    if (!XTranslateCoordinates(display_, target_, root_, 0, 0, &state.x, &state.y, &child))
        return state;
    state.width = static_cast<unsigned>(attributes.width);
    state.height = static_cast<unsigned>(attributes.height);
    // IsViewable requires every ancestor to be mapped: hidden workspaces,
    // minimized frames and withdrawn windows all report otherwise.
    state.viewable = attributes.map_state == IsViewable;
    state.exists = true;
    return state;
}

bool Tracker::affects(const XEvent& event, Window ignored) const noexcept {
    Window window = 0;
    switch (event.type) {
        case ConfigureNotify:
            window = event.xconfigure.window;
            break;
        case MapNotify:
            window = event.xmap.window;
            break;
        case UnmapNotify:
            window = event.xunmap.window;
            break;
        case ReparentNotify:
            window = event.xreparent.window;
            break;
        case CirculateNotify:
            window = event.xcirculate.window;
            break;
        case DestroyNotify:
            window = event.xdestroywindow.window;
            break;
        default:
            return false;
    }
    // Any other top-level's restack can bury or uncover the target; changes to
    // the target and its frame move or hide it.
    return window != ignored;
}

bool Tracker::destroyed(const XEvent& event) const noexcept {
    return event.type == DestroyNotify &&
           (event.xdestroywindow.window == target_ || event.xdestroywindow.window == frame_);
}

bool Tracker::stack_above(Window window) const {
    Window root = 0, parent = 0;
    Children children;
    if (!XQueryTree(display_, root_, &root, &parent, &children.windows, &children.count))
        return false;
    // Children are listed bottom to top.
    for (unsigned i = 0; i + 1 < children.count; ++i)
        if (children.windows[i] == frame_ && children.windows[i + 1] == window)
            return false;
    XWindowChanges changes{};
    changes.sibling = frame_;
    changes.stack_mode = Above;
    XConfigureWindow(display_, window, CWSibling | CWStackMode, &changes);
    return true;
}
}  // namespace glyphshift::overlay
