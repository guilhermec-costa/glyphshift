#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/process.hpp>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/display/win32/global_hotkey.hpp"
#include "../../src/display/win32/tray.hpp"
#include "daemon.hpp"
#include "options.hpp"

// Background mode on Windows: one global hotkey attaches a capture overlay to
// the foreground game window, or detaches it. Each attachment is a separate
// `glyphshift-overlay --window HANDLE` process (in a job that ends with the
// daemon), so every game keeps its own editor session and preset profile, and
// one failure cannot affect another game.
namespace glyphshift::overlay {
namespace {
namespace win32 = display::win32;

// Background launches (menu, sign-in) have no terminal: messages reach the
// user as notifications from the icon. GLYPHSHIFT_NO_NOTIFICATIONS=1 (tests)
// turns them off.
void notify(win32::Tray& tray, const std::string& summary, const std::string& body) {
    const char* quiet = std::getenv("GLYPHSHIFT_NO_NOTIFICATIONS");
    if (!(quiet && *quiet == '1'))
        tray.notify(summary, body);
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

platform::ProcessId spawn(const std::string& program, HWND window, const DaemonOptions& options) {
    std::vector<std::string> argv{program,
        "--window",
        std::to_string(reinterpret_cast<std::uintptr_t>(window))};
    if (!options.preset.empty())
        argv.insert(argv.end(), {"--preset", options.preset});
    if (options.log)
        argv.push_back("--log");
    return platform::spawn_child(argv);
}
}  // namespace

int run_daemon(const DaemonOptions& options) {
    win32::GlobalHotkey::check(options.hotkey);
    const auto path = platform::executable_path().u8string();
    const std::string program(path.begin(), path.end());
    win32::GlobalHotkey hotkey(options.hotkey);
    if (!hotkey.claimed()) {
        const auto& other = hotkey.running();
        std::fprintf(stderr,
            "glyphshift-overlay: GlyphShift is already running. Press %s in a game to attach "
            "or detach.\n",
            other.empty() ? "its hotkey" : hotkey_name(other).c_str());
        // Without a terminal, the running daemon's icon shows the reminder.
        if (!platform::stderr_is_terminal())
            hotkey.remind_running();
        return 0;
    }
    const char* no_focus = std::getenv("GLYPHSHIFT_OVERLAY_NO_FOCUS");
    const auto tray = std::make_unique<win32::Tray>(
        "GlyphShift: press " + hotkey_name(options.hotkey) + " in a game",
        !(no_focus && *no_focus == '1'));
    try {
        hotkey.grab();
    } catch (const std::runtime_error&) {
        if (!platform::stderr_is_terminal())
            notify(*tray,
                "GlyphShift could not start",
                "The hotkey " + hotkey_name(options.hotkey) +
                    " is taken by another application; choose another with --hotkey.");
        // The notification goes with the icon: keep it until it is dismissed
        // or has timed out.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (tray->notifying() && std::chrono::steady_clock::now() < deadline) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
        }
        throw std::runtime_error("the hotkey " + options.hotkey +
                                 " is already taken by another application; choose --hotkey");
    }
    platform::catch_stop_requests();
    std::fprintf(stderr,
        "glyphshift-overlay: running; press %s in a game to attach or detach\n",
        options.hotkey.c_str());
    if (!platform::stderr_is_terminal())
        notify(*tray,
            "GlyphShift is running",
            "Press " + hotkey_name(options.hotkey) + " in a game to attach or detach.");
    auto current = options;
    std::map<HWND, Attached> attached;
    const auto toggle = [&](HWND game) {
        if (const auto found = attached.find(game); found != attached.end()) {
            platform::stop_child(found->second.process);
            std::fprintf(stderr,
                "glyphshift-overlay: detached %llu\n",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(game)));
        } else if (const auto child = spawn(program, game, current); child > 0) {
            attached[game] = {child, win32::window_title(game)};
            std::fprintf(stderr,
                "glyphshift-overlay: attached %llu (%s)\n",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(game)),
                attached[game].title.c_str());
        }
    };
    // The icon's menu: attached games (choose one to detach it), the style for
    // new attachments, start at sign-in and quit.
    std::vector<HWND> listed;
    const auto menu = [&] {
        const win32::MenuItem separator{"", -1, false, false, true};
        std::vector<win32::MenuItem> rows;
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
            const HWND game = listed[static_cast<std::size_t>(id - first_game)];
            if (attached.count(game))
                toggle(game);
        }
    };
    while (!platform::stop_requested()) {
        // Reap overlays that ended (game closed or overlay failed).
        for (const auto ended : platform::ended_children())
            for (auto it = attached.begin(); it != attached.end(); ++it)
                if (it->second.process == ended) {
                    attached.erase(it);
                    break;
                }
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (hotkey.reminder(message)) {
                // A second launch without a terminal: its user sees this.
                std::fprintf(stderr,
                    "glyphshift-overlay: started again; reminding of the hotkey\n");
                notify(*tray,
                    "GlyphShift is already running",
                    "Press " + hotkey_name(current.hotkey) + " in a game to attach or detach.");
                continue;
            }
            if (const int id = tray->handle(message, menu); id >= 0) {
                choose(id);
                continue;
            }
            if (hotkey.pressed(message)) {
                const HWND game = win32::focused_window();
                if (!game)
                    std::fprintf(stderr, "glyphshift-overlay: no focused window to attach to\n");
                else
                    toggle(game);
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
    }
    for (const auto& [window, game] : attached) {
        (void)window;
        platform::stop_child(game.process);
    }
    for (const auto& [window, game] : attached) {
        (void)window;
        platform::wait_child(game.process);
    }
    return 0;
}
}  // namespace glyphshift::overlay
