#pragma once
#include <filesystem>

// Per-user locations of the platform layer (Linux: XDG base directories).
namespace glyphshift::platform {
// The user's configuration directory: absolute XDG_CONFIG_HOME, else
// HOME/.config. Throws when neither is usable.
std::filesystem::path config_home();
}  // namespace glyphshift::platform
