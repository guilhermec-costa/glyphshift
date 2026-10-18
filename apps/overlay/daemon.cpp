#include "daemon.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/process.hpp>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Background mode: one global hotkey attaches a capture overlay to the focused
// game window, or detaches it. Each attachment is a separate
// `glyphshift-overlay --window ID` process, so every game keeps its own editor
// session and preset profile, and one failure cannot affect another game.
namespace glyphshift::overlay {
namespace {
volatile sig_atomic_t stop = 0;
bool grab_failed = false;

int record_error(Display*, XErrorEvent* event) {
    if (event->error_code == BadAccess)
        grab_failed = true;
    return 0;
}

struct Hotkey {
    unsigned modifiers = 0;
    KeySym symbol = NoSymbol;
};

Hotkey parse_hotkey(const std::string& text) {
    Hotkey hotkey;
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

// The managed client (a window with WM_STATE) that contains `window`, so Wine's
// internal presentation children resolve to the game's top-level client.
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

// The keyboard focus resolved to its managed client; falls back to the window
// manager's _NET_ACTIVE_WINDOW (EWMH) when focus is on the root or unknown.
Window focused_game(Display* display) {
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

// "ctrl+alt+g" as "Ctrl+Alt+G" for messages.
std::string display_name(const std::string& hotkey) {
    std::string result;
    bool start = true;
    for (const char c : hotkey) {
        const auto ch = static_cast<unsigned char>(c);
        result += start ? static_cast<char>(std::toupper(ch)) : c;
        start = c == '+';
    }
    return result;
}

// Background launches (menu, autostart) have no terminal: tell the user
// through a desktop notification instead.
void tell(const std::string& summary, const std::string& body) {
    std::fprintf(stderr, "glyphshift-overlay: %s. %s\n", summary.c_str(), body.c_str());
    if (!isatty(STDERR_FILENO))
        platform::notify(summary, body);
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

// One daemon per X display. Like window managers and compositors, it owns a
// selection, held by a hidden window that also publishes its hotkey; the
// selection is released when the process exits. Returns the owner window, or
// 0 with `other` set to the running daemon's hotkey.
Window claim_instance(Display* display, const std::string& hotkey, std::string& other) {
    const Window root = DefaultRootWindow(display);
    const Atom selection = XInternAtom(display,
        ("_GLYPHSHIFT_DAEMON_S" + std::to_string(DefaultScreen(display))).c_str(),
        False);
    const Atom property = XInternAtom(display, "_GLYPHSHIFT_HOTKEY", False);
    // The server grab makes looking for an owner and taking ownership one
    // step, so of two daemons started together exactly one stays.
    XGrabServer(display);
    const Window owner = XGetSelectionOwner(display, selection);
    Window window = 0;
    if (owner) {
        other = read_text(display, owner, property);
    } else {
        window =
            XCreateWindow(display, root, -1, -1, 1, 1, 0, 0, InputOnly, CopyFromParent, 0, nullptr);
        XChangeProperty(display,
            window,
            property,
            XA_STRING,
            8,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(hotkey.data()),
            static_cast<int>(hotkey.size()));
        XSetSelectionOwner(display, selection, window, CurrentTime);
    }
    XUngrabServer(display);
    XSync(display, False);
    return window;
}

pid_t spawn(const std::string& program, Window window, const DaemonOptions& options) {
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child < 0)
        return -1;
    if (child)
        return child;
    // Overlays end with the daemon, even if it is killed.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() != parent)
        _exit(0);
    const std::string id = std::to_string(window);
    std::vector<const char*> argv{program.c_str(), "--window", id.c_str()};
    if (!options.preset.empty()) {
        argv.push_back("--preset");
        argv.push_back(options.preset.c_str());
    }
    if (options.log)
        argv.push_back("--log");
    argv.push_back(nullptr);
    execv(program.c_str(), const_cast<char* const*>(argv.data()));
    _exit(127);
}
}  // namespace

void check_hotkey(const std::string& hotkey) {
    (void)parse_hotkey(hotkey);
}

int run_daemon(const DaemonOptions& options) {
    const auto hotkey = parse_hotkey(options.hotkey);
    const std::string program = platform::executable_path();
    Display* display = XOpenDisplay(nullptr);
    if (!display)
        throw std::runtime_error("cannot open X11 display");
    const Window root = DefaultRootWindow(display);
    const KeyCode code = XKeysymToKeycode(display, hotkey.symbol);
    if (!code)
        throw std::invalid_argument("the hotkey's key is not on this keyboard");
    XSetErrorHandler(record_error);
    std::string other;
    if (!claim_instance(display, options.hotkey, other)) {
        XCloseDisplay(display);
        tell("GlyphShift is already running",
            other.empty() ? "Press its hotkey in a game to attach or detach."
                          : "Press " + display_name(other) + " in a game to attach or detach.");
        return 0;
    }
    // Grab with and without CapsLock and NumLock so the hotkey always works.
    for (const unsigned locks :
        {0U, unsigned{LockMask}, unsigned{Mod2Mask}, unsigned{LockMask | Mod2Mask}})
        XGrabKey(display, code, hotkey.modifiers | locks, root, True, GrabModeAsync, GrabModeAsync);
    XSync(display, False);
    if (grab_failed) {
        XCloseDisplay(display);
        if (!isatty(STDERR_FILENO))
            platform::notify("GlyphShift could not start",
                "The hotkey " + display_name(options.hotkey) +
                    " is taken by another application; choose another with --hotkey.");
        throw std::runtime_error("the hotkey " + options.hotkey +
                                 " is already taken by another application; choose --hotkey");
    }
    struct sigaction action{};
    action.sa_handler = [](int) {
        stop = 1;
    };
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    std::fprintf(stderr,
        "glyphshift-overlay: running; press %s in a game to attach or detach\n",
        options.hotkey.c_str());
    if (!isatty(STDERR_FILENO))
        platform::notify("GlyphShift is running",
            "Press " + display_name(options.hotkey) + " in a game to attach or detach.");
    std::map<Window, pid_t> attached;
    auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (!stop) {
        // Reap overlays that ended (game closed or overlay failed).
        for (pid_t pid; (pid = waitpid(-1, nullptr, WNOHANG)) > 0;)
            for (auto it = attached.begin(); it != attached.end(); ++it)
                if (it->second == pid) {
                    attached.erase(it);
                    break;
                }
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != KeyPress)
                continue;
            // Ignore key auto-repeat and double presses.
            const auto now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::milliseconds(400))
                continue;
            last = now;
            const Window game = focused_game(display);
            if (!game) {
                std::fprintf(stderr, "glyphshift-overlay: no focused window to attach to\n");
                continue;
            }
            if (const auto found = attached.find(game); found != attached.end()) {
                kill(found->second, SIGTERM);
                std::fprintf(stderr, "glyphshift-overlay: detached 0x%lx\n", game);
            } else if (const pid_t child = spawn(program, game, options); child > 0) {
                attached[game] = child;
                std::fprintf(stderr, "glyphshift-overlay: attached 0x%lx\n", game);
            }
        }
        const int fd = ConnectionNumber(display);
        fd_set read{};
        FD_ZERO(&read);
        FD_SET(fd, &read);
        timeval timeout{0, 200000};
        select(fd + 1, &read, nullptr, nullptr, &timeout);
    }
    for (const auto& [window, pid] : attached) {
        (void)window;
        kill(pid, SIGTERM);
    }
    for (const auto& [window, pid] : attached) {
        (void)window;
        waitpid(pid, nullptr, 0);
    }
    XUngrabKey(display, code, AnyModifier, root);
    XCloseDisplay(display);
    return 0;
}
}  // namespace glyphshift::overlay
