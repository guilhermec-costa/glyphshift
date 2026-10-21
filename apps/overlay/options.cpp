#include "options.hpp"

#include <cctype>
#include <cstdlib>
#include <glyphshift/platform/process.hpp>
#include <stdexcept>
#include <string_view>

namespace glyphshift::overlay {
const char* const usage =
    "Usage: glyphshift-overlay [--hotkey KEYS] [--preset NAME] [--log]\n"
    "       glyphshift-overlay --window ID [--preset NAME] [--log]\n"
    "       glyphshift-overlay --autostart on|off [--hotkey KEYS] [--preset NAME]\n";

std::string hotkey_name(const std::string& hotkey) {
    std::string result;
    bool start = true;
    for (const char c : hotkey) {
        const auto ch = static_cast<unsigned char>(c);
        result += start ? static_cast<char>(std::toupper(ch)) : c;
        start = c == '+';
    }
    return result;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--window" && i + 1 < argc)
            options.target = std::strtoull(argv[++i], nullptr, 0);
        else if (argument == "--preset" && i + 1 < argc)
            options.preset = argv[++i];
        else if (argument == "--hotkey" && i + 1 < argc)
            options.hotkey = argv[++i];
        else if (argument == "--autostart" && i + 1 < argc)
            options.autostart = argv[++i];
        else if (argument == "--log")
            options.log = true;
        else
            throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
    if (options.target && !options.hotkey.empty())
        throw std::invalid_argument("--hotkey applies only without --window");
    if (!options.autostart.empty() &&
        (options.target || (options.autostart != "on" && options.autostart != "off")))
        throw std::invalid_argument("--autostart takes on or off, without --window");
    return options;
}

// Profile names allow 1-64 ASCII letters, digits, dots, underscores and
// hyphens.
void choose_profile(const std::string& application) {
    if (const char* chosen = std::getenv("GLYPHSHIFT_PROFILE"); chosen && *chosen)
        return;
    std::string profile = "overlay";
    std::string suffix;
    for (const char c : application) {
        if (suffix.size() >= 55)
            break;
        const auto ch = static_cast<unsigned char>(c);
        suffix += std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-'
                      ? static_cast<char>(std::tolower(ch))
                      : '-';
    }
    if (!suffix.empty())
        profile += "-" + suffix;
    platform::set_environment("GLYPHSHIFT_PROFILE", profile);
}
}  // namespace glyphshift::overlay
