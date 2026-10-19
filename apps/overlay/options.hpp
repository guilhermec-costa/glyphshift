#pragma once
#include <cstdint>
#include <string>

// The capture overlay's command line and per-game profile rule, shared by
// every window system.
namespace glyphshift::overlay {
struct Options {
    // The window to attach to (X11 window ID, Win32 window handle); 0 runs the
    // background daemon.
    std::uint64_t target = 0;
    std::string preset;
    std::string hotkey;
    std::string autostart;
    bool log = false;
};

// Throws std::invalid_argument with the reason.
Options parse(int argc, char** argv);

extern const char* const usage;

// Presets are kept per game: profile "overlay-NAME" from the target's
// application name (X11: its class; Windows: its executable), unless the user
// chose GLYPHSHIFT_PROFILE.
void choose_profile(const std::string& application);
}  // namespace glyphshift::overlay
