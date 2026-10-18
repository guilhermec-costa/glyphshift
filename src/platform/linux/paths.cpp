#include <cstdlib>
#include <glyphshift/platform/paths.hpp>
#include <stdexcept>

namespace glyphshift::platform {
std::filesystem::path config_home() {
    if (const char* value = std::getenv("XDG_CONFIG_HOME");
        value && *value && std::filesystem::path(value).is_absolute())
        return value;
    if (const char* home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        return std::filesystem::path(home) / ".config";
    throw std::runtime_error("needs an absolute XDG_CONFIG_HOME or HOME");
}
}  // namespace glyphshift::platform
