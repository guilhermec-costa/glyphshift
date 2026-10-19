#pragma once
#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

// Environment variables and temporary directories for the unit tests, on
// Linux and Windows alike.
namespace glyphshift::test {
// The variable that places the user's configuration directory (presets):
// XDG_CONFIG_HOME on Linux, APPDATA on Windows.
#ifdef _WIN32
inline constexpr const char* config_home_variable = "APPDATA";
#else
inline constexpr const char* config_home_variable = "XDG_CONFIG_HOME";
#endif

// An absolute path to a font file that does not exist.
#ifdef _WIN32
inline constexpr const char* missing_font = "C:\\missing\\glyphshift\\font.ttf";
#else
inline constexpr const char* missing_font = "/missing/font.ttf";
#endif

inline void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

inline void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// A new, empty directory under the system's temporary directory.
inline std::filesystem::path temporary_directory(const std::string& prefix) {
    std::random_device device;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto path = std::filesystem::temp_directory_path() /
                          (prefix + "-" + std::to_string(device() % 1000000000U));
        if (std::filesystem::create_directory(path))
            return path;
    }
    throw std::runtime_error("cannot create a temporary directory");
}
}  // namespace glyphshift::test
