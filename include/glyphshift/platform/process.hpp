#pragma once
#include <filesystem>

// Process facilities of the platform layer (Linux: /proc).
namespace glyphshift::platform {
// Absolute path of the running executable, symbolic links resolved.
std::filesystem::path executable_path();
}  // namespace glyphshift::platform
