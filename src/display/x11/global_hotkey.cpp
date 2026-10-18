#include "global_hotkey.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cctype>
#include <stdexcept>

namespace glyphshift::display::x11 {
namespace {
bool grab_failed = false;

int record_error(Display*, XErrorEvent* event) {
    if (event->error_code == BadAccess)
        grab_failed = true;
    return 0;
}

struct Parsed {
    unsigned modifiers = 0;
    KeySym symbol = NoSymbol;
};

Parsed parse(const std::string& text) {
    Parsed hotkey;
    std::size_t start = 0;
    while (true) {
        const auto end = text.find('+', start);
        std::string token = text.substr(start, end == std::string::npos ? end : end - start);
        std::string lower;
        for (const char c : token)
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (end == std::string::npos) {
            hotkey.symbol = XStringToKeysym(token.c_str());
            if (hotkey.symbol == NoSymbol && token.size() == 1)
                hotkey.symbol = XStringToKeysym(lower.c_str());
            break;
        }
        if (lower == "ctrl" || lower == "control")
            hotkey.modifiers |= ControlMask;
        else if (lower == "shift")
            hotkey.modifiers |= ShiftMask;
        else if (lower == "alt" || lower == "mod1")
            hotkey.modifiers |= Mod1Mask;
        else if (lower == "super" || lower == "win" || lower == "mod4")
            hotkey.modifiers |= Mod4Mask;
        else
            throw std::invalid_argument("unknown hotkey modifier: " + token);
        start = end + 1;
    }
    if (hotkey.symbol == NoSymbol)
        throw std::invalid_argument("unknown hotkey key in: " + text);
    return hotkey;
}

std::string read_text(Display* display, Window window, Atom property) {
    Atom type = None;
    int format = 0;
    unsigned long items = 0, remaining = 0;
    unsigned char* data = nullptr;
    std::string text;
    if (XGetWindowProperty(display,
            window,
            property,
            0,
            64,
            False,
            AnyPropertyType,
            &type,
            &format,
            &items,
            &remaining,
            &data) == Success &&
        data && format == 8)
        text.assign(reinterpret_cast<const char*>(data), items);
    if (data)
        XFree(data);
    return text;
}

// The managed client (a window with WM_STATE) that contains `window`.
// Without a window manager, the top-level window itself.
Window managed_client(Display* display, Window window) {
    const Atom state = XInternAtom(display, "WM_STATE", True);
    const Window root = DefaultRootWindow(display);
    for (unsigned depth = 0; window && window != root && depth < 32; ++depth) {
        if (state) {
            Atom type = None;
            int format = 0;
            unsigned long items = 0, remaining = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(display,
                    window,
                    state,
                    0,
                    0,
                    False,
                    AnyPropertyType,
                    &type,
                    &format,
                    &items,
                    &remaining,
                    &data) == Success) {
                if (data)
                    XFree(data);
                if (type != None)
                    return window;
            }
        }
        Window parent_root = 0, parent = 0, *children = nullptr;
        unsigned count = 0;
        if (!XQueryTree(display, window, &parent_root, &parent, &children, &count))
            return 0;
        if (children)
            XFree(children);
        if (parent == root)
            return window;
        window = parent;
    }
    return 0;
}
}  // namespace

void GlobalHotkey::check(const std::string& hotkey) {
    (void)parse(hotkey);
}

GlobalHotkey::GlobalHotkey(Display* display, const std::string& hotkey)
    : display_(display), root_(DefaultRootWindow(display)) {
    const auto parsed = parse(hotkey);
    modifiers_ = parsed.modifiers;
    code_ = XKeysymToKeycode(display_, parsed.symbol);
    if (!code_)
        throw std::invalid_argument("the hotkey's key is not on this keyboard");
    XSetErrorHandler(record_error);
    const Atom selection = XInternAtom(display_,
        ("_GLYPHSHIFT_DAEMON_S" + std::to_string(DefaultScreen(display_))).c_str(),
        False);
    const Atom property = XInternAtom(display_, "_GLYPHSHIFT_HOTKEY", False);
    // The server grab makes looking for an owner and taking ownership one
    // step, so of two daemons started together exactly one stays.
    XGrabServer(display_);
    const Window owner = XGetSelectionOwner(display_, selection);
    if (owner) {
        other_ = read_text(display_, owner, property);
    } else {
        owner_ = XCreateWindow(display_,
            root_,
            -1,
            -1,
            1,
            1,
            0,
            0,
            InputOnly,
            CopyFromParent,
            0,
            nullptr);
        XChangeProperty(display_,
            owner_,
            property,
            XA_STRING,
            8,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(hotkey.data()),
            static_cast<int>(hotkey.size()));
        XSetSelectionOwner(display_, selection, owner_, CurrentTime);
    }
    XUngrabServer(display_);
    XSync(display_, False);
}

GlobalHotkey::~GlobalHotkey() {
    if (grabbed_)
        XUngrabKey(display_, code_, AnyModifier, root_);
}

void GlobalHotkey::grab() {
    // Grab with and without CapsLock and NumLock so the hotkey always works.
    for (const unsigned locks :
        {0U, unsigned{LockMask}, unsigned{Mod2Mask}, unsigned{LockMask | Mod2Mask}})
        XGrabKey(display_, code_, modifiers_ | locks, root_, True, GrabModeAsync, GrabModeAsync);
    XSync(display_, False);
    if (grab_failed)
        throw std::runtime_error("hotkey taken");
    grabbed_ = true;
}

bool GlobalHotkey::owns(const XEvent& event) const noexcept {
    return event.type == KeyPress && event.xkey.window == root_;
}

bool GlobalHotkey::pressed(const XEvent& event) {
    if (!owns(event))
        return false;
    // Ignore key auto-repeat and double presses.
    const auto now = std::chrono::steady_clock::now();
    if (now - last_ < std::chrono::milliseconds(400))
        return false;
    last_ = now;
    return true;
}

Window focused_client(Display* display) {
    Window focus = 0;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);
    if (focus > PointerRoot) {
        if (const Window client = managed_client(display, focus))
            return client;
    }
    const Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    if (!active)
        return 0;
    Atom type = None;
    int format = 0;
    unsigned long items = 0, remaining = 0;
    unsigned char* data = nullptr;
    Window result = 0;
    if (XGetWindowProperty(display,
            DefaultRootWindow(display),
            active,
            0,
            1,
            False,
            XA_WINDOW,
            &type,
            &format,
            &items,
            &remaining,
            &data) == Success &&
        data && items == 1)
        result = static_cast<Window>(*reinterpret_cast<unsigned long*>(data));
    if (data)
        XFree(data);
    return result;
}

std::string window_title(Display* display, Window window) {
    std::string title = read_text(display, window, XInternAtom(display, "_NET_WM_NAME", False));
    if (title.empty())
        title = read_text(display, window, XA_WM_NAME);
    if (title.size() > 48)
        title = title.substr(0, 45) + "...";
    return title.empty() ? "untitled window" : title;
}
}  // namespace glyphshift::display::x11
