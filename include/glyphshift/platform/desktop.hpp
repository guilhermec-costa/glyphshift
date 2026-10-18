#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Desktop integration of the background overlay: start-on-login and user
// notifications. Platform layer: implemented per operating system (Linux:
// XDG autostart and freedesktop notifications through notify-send).
namespace glyphshift::platform {
// The user's configuration directory: absolute XDG_CONFIG_HOME, else
// HOME/.config. Throws when neither is usable.
std::filesystem::path config_home();

// Quotes one argument for a desktop entry's Exec key (Desktop Entry
// Specification: reserved characters quoted, then string escaping).
std::string desktop_exec_argument(const std::string& argument);

// The start-on-login entry for the background overlay.
std::filesystem::path autostart_entry();
bool autostart_enabled();

// Writes (atomically) an entry that runs `command` at login; command[0] is an
// absolute path to the program.
void enable_autostart(const std::vector<std::string>& command);

// Removes the entry; false when there was none.
bool disable_autostart();

// Shows a desktop notification when a notification service is available.
// Never waits for it and never fails the caller.
void notify(const std::string& summary, const std::string& body);
}  // namespace glyphshift::platform
