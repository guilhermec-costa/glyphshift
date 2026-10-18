#pragma once
#include <string>

namespace glyphshift::overlay {
struct DaemonOptions {
    std::string hotkey = "ctrl+alt+g";
    std::string preset;
    bool log = false;
};

// Throws std::invalid_argument when `hotkey` cannot be parsed.
void check_hotkey(const std::string& hotkey);

// One daemon per X display: a second one reports the running daemon's hotkey
// and returns 0. Waits for the global hotkey and attaches or detaches an overlay process for
// the focused window. Returns when interrupted; overlays end with it.
int run_daemon(const DaemonOptions& options);
}  // namespace glyphshift::overlay
