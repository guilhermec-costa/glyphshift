#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/process.hpp>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/display/win32/global_hotkey.hpp"
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

// Without a terminal (started from the menu or at sign-in) messages also go
// to a notification, once the notification-area icon provides them.
void tell(const std::string& summary, const std::string& body) {
    std::fprintf(stderr, "glyphshift-overlay: %s. %s\n", summary.c_str(), body.c_str());
    if (!platform::stderr_is_terminal())
        platform::notify(summary, body);
}

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
        tell("GlyphShift is already running",
            other.empty() ? "Press its hotkey in a game to attach or detach."
                          : "Press " + hotkey_name(other) + " in a game to attach or detach.");
        return 0;
    }
    try {
        hotkey.grab();
    } catch (const std::runtime_error&) {
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
    std::map<HWND, Attached> attached;
    const auto toggle = [&](HWND game) {
        if (const auto found = attached.find(game); found != attached.end()) {
            platform::stop_child(found->second.process);
            std::fprintf(stderr,
                "glyphshift-overlay: detached %llu\n",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(game)));
        } else if (const auto child = spawn(program, game, options); child > 0) {
            attached[game] = {child, win32::window_title(game)};
            std::fprintf(stderr,
                "glyphshift-overlay: attached %llu (%s)\n",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(game)),
                attached[game].title.c_str());
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
