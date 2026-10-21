#include "daemon.hpp"

#include <X11/Xlib.h>
#include <sys/select.h>

#include <cstdio>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/process.hpp>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/display/x11/global_hotkey.hpp"
#include "../../src/display/x11/tray.hpp"
#include "options.hpp"

// Background mode: one global hotkey attaches a capture overlay to the focused
// game window, or detaches it. Each attachment is a separate
// `glyphshift-overlay --window ID` process, so every game keeps its own editor
// session and preset profile, and one failure cannot affect another game.
namespace glyphshift::overlay {
namespace {
namespace x11 = display::x11;

// Background launches (menu, autostart) have no terminal: tell the user
// through a desktop notification instead.
void tell(const std::string& summary, const std::string& body) {
    std::fprintf(stderr, "glyphshift-overlay: %s. %s\n", summary.c_str(), body.c_str());
    if (!platform::stderr_is_terminal())
        platform::notify(summary, body);
}

// Tray menu row ids.
enum : int {
    own_style = 100,
    first_style = 101,
    start_at_login = 200,
    quit = 300,
    first_game = 1000,
};

struct Attached {
    platform::ProcessId process;
    std::string title;
};

// One overlay process per game; it ends with the daemon, even if the daemon
// is killed.
platform::ProcessId spawn(const std::string& program,
    unsigned long window,
    const DaemonOptions& options) {
    std::vector<std::string> argv{program, "--window", std::to_string(window)};
    if (!options.preset.empty())
        argv.insert(argv.end(), {"--preset", options.preset});
    if (options.log)
        argv.push_back("--log");
    return platform::spawn_child(argv);
}
}  // namespace

int run_daemon(const DaemonOptions& options) {
    x11::GlobalHotkey::check(options.hotkey);
    const std::string program = platform::executable_path();
    Display* display = XOpenDisplay(nullptr);
    if (!display)
        throw std::runtime_error("cannot open X11 display");
    std::unique_ptr<x11::GlobalHotkey> hotkey;
    try {
        hotkey = std::make_unique<x11::GlobalHotkey>(display, options.hotkey);
    } catch (...) {
        XCloseDisplay(display);
        throw;
    }
    if (!hotkey->claimed()) {
        const std::string other = hotkey->running();
        hotkey.reset();
        XCloseDisplay(display);
        tell("GlyphShift is already running",
            other.empty() ? "Press its hotkey in a game to attach or detach."
                          : "Press " + hotkey_name(other) + " in a game to attach or detach.");
        return 0;
    }
    try {
        hotkey->grab();
    } catch (const std::runtime_error&) {
        hotkey.reset();
        XCloseDisplay(display);
        if (!platform::stderr_is_terminal())
            platform::notify("GlyphShift could not start",
                "The hotkey " + hotkey_name(options.hotkey) +
                    " is taken by another application; choose another with --hotkey.");
        throw std::runtime_error("the hotkey " + options.hotkey +
                                 " is already taken by another application; choose --hotkey");
    }
    platform::catch_stop_requests();
    std::fprintf(stderr,
        "glyphshift-overlay: running; press %s in a game to attach or detach\n",
        options.hotkey.c_str());
    if (!platform::stderr_is_terminal())
        platform::notify("GlyphShift is running",
            "Press " + hotkey_name(options.hotkey) + " in a game to attach or detach.");
    auto current = options;
    std::map<Window, Attached> attached;
    const auto toggle = [&](Window game) {
        if (const auto found = attached.find(game); found != attached.end()) {
            platform::stop_child(found->second.process);
            std::fprintf(stderr, "glyphshift-overlay: detached 0x%lx\n", game);
        } else if (const auto child = spawn(program, game, current); child > 0) {
            attached[game] = {child, x11::window_title(display, game)};
            std::fprintf(stderr, "glyphshift-overlay: attached 0x%lx\n", game);
        }
    };
    // The tray menu: attached games (choose one to detach it), the style for
    // new attachments, start at login and quit.
    std::vector<Window> listed;
    const auto menu = [&] {
        const x11::MenuItem separator{"", -1, false, false, true};
        std::vector<x11::MenuItem> rows;
        rows.push_back({"GlyphShift: press " + hotkey_name(current.hotkey) + " in a game"});
        rows.push_back(separator);
        listed.clear();
        for (const auto& [window, game] : attached) {
            rows.push_back(
                {"Detach from " + game.title, first_game + static_cast<int>(listed.size())});
            listed.push_back(window);
        }
        if (attached.empty())
            rows.push_back({"No game attached"});
        rows.push_back(separator);
        rows.push_back({"Style for newly attached games"});
        rows.push_back({"Each game's saved style", own_style, true, current.preset.empty()});
        int id = first_style;
        for (const auto& style : builtin_presets())
            rows.push_back({std::string(style.title), id++, true, current.preset == style.name});
        rows.push_back(separator);
        rows.push_back({"Start at login", start_at_login, true, platform::autostart_enabled()});
        rows.push_back({"Quit GlyphShift", quit});
        return rows;
    };
    const auto choose = [&](int id) {
        if (id == quit) {
            platform::request_stop();
        } else if (id == start_at_login) {
            try {
                if (platform::autostart_enabled()) {
                    platform::disable_autostart();
                } else {
                    std::vector<std::string> command{program};
                    if (current.hotkey != DaemonOptions{}.hotkey)
                        command.insert(command.end(), {"--hotkey", current.hotkey});
                    if (!current.preset.empty())
                        command.insert(command.end(), {"--preset", current.preset});
                    platform::enable_autostart(command);
                }
                std::fprintf(stderr,
                    "glyphshift-overlay: start at login %s\n",
                    platform::autostart_enabled() ? "on" : "off");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "glyphshift-overlay: start at login: %s\n", error.what());
            }
        } else if (id == own_style || (id >= first_style && id < start_at_login)) {
            const auto styles = builtin_presets();
            const auto index = static_cast<std::size_t>(id - first_style);
            current.preset = id == own_style || index >= styles.size()
                                 ? std::string()
                                 : std::string(styles[index].name);
            std::fprintf(stderr,
                "glyphshift-overlay: newly attached games use %s\n",
                current.preset.empty() ? "their saved style" : current.preset.c_str());
        } else if (id >= first_game && static_cast<std::size_t>(id - first_game) < listed.size()) {
            const Window game = listed[static_cast<std::size_t>(id - first_game)];
            if (attached.count(game))
                toggle(game);
        }
    };
    auto tray = std::make_unique<x11::Tray>(display);
    while (!platform::stop_requested()) {
        // Reap overlays that ended (game closed or overlay failed).
        for (const auto ended : platform::ended_children())
            for (auto it = attached.begin(); it != attached.end(); ++it)
                if (it->second.process == ended) {
                    attached.erase(it);
                    break;
                }
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (!hotkey->owns(event)) {
                if (const int id = tray->handle(event, menu); id >= 0)
                    choose(id);
                continue;
            }
            if (!hotkey->pressed(event))
                continue;
            const Window game = x11::focused_client(display);
            if (!game) {
                std::fprintf(stderr, "glyphshift-overlay: no focused window to attach to\n");
                continue;
            }
            toggle(game);
        }
        const int fd = ConnectionNumber(display);
        fd_set read{};
        FD_ZERO(&read);
        FD_SET(fd, &read);
        timeval timeout{0, 200000};
        select(fd + 1, &read, nullptr, nullptr, &timeout);
    }
    for (const auto& [window, game] : attached) {
        (void)window;
        platform::stop_child(game.process);
    }
    for (const auto& [window, game] : attached) {
        (void)window;
        platform::wait_child(game.process);
    }
    tray.reset();  // Uses the connection: before it closes.
    hotkey.reset();
    XCloseDisplay(display);
    return 0;
}
}  // namespace glyphshift::overlay
