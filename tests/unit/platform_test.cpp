#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/process.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}

std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}
}  // namespace

int main() {
    using namespace glyphshift::platform;
    char pattern[] = "/tmp/glyphshift-platform-XXXXXX";
    const char* root = mkdtemp(pattern);
    if (!root)
        return 1;
    const std::filesystem::path path = root;
    try {
        // Desktop Entry Exec quoting: plain arguments stay bare; reserved
        // characters are quoted, ", `, $ and \ escaped inside quotes, then
        // backslashes doubled by string escaping; % doubled.
        check(desktop_exec_argument("/usr/bin/glyphshift-overlay") == "/usr/bin/glyphshift-overlay",
            "plain path unquoted");
        check(desktop_exec_argument("ctrl+alt+g") == "ctrl+alt+g", "hotkey unquoted");
        check(desktop_exec_argument("/home/a b/x") == "\"/home/a b/x\"", "space quoted");
        check(desktop_exec_argument("a\"b") == "\"a\\\\\"b\"", "quote escaped");
        check(desktop_exec_argument("a\\b") == "\"a\\\\\\\\b\"", "backslash escaped twice");
        check(desktop_exec_argument("$HOME") == "\"\\\\$HOME\"", "dollar escaped");
        check(desktop_exec_argument("50%") == "50%%", "percent doubled");
        check(desktop_exec_argument("") == "\"\"", "empty argument kept");
        bool rejected = false;
        try {
            (void)desktop_exec_argument("a\nb");
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "line break rejected");

        // config_home: absolute XDG_CONFIG_HOME, else HOME/.config.
        setenv("HOME", (path / "home").c_str(), 1);
        setenv("XDG_CONFIG_HOME", "relative/config", 1);
        check(config_home() == path / "home/.config", "relative XDG_CONFIG_HOME ignored");
        setenv("XDG_CONFIG_HOME", (path / "config").c_str(), 1);
        check(config_home() == path / "config", "absolute XDG_CONFIG_HOME used");

        // Autostart entry: written atomically, replaced, removed once.
        const auto entry = path / "config/autostart/glyphshift.desktop";
        check(autostart_entry() == entry && !autostart_enabled(), "no entry initially");
        check(!disable_autostart(), "removing a missing entry reports false");
        enable_autostart({"/opt/glyph shift/glyphshift-overlay", "--hotkey", "super+F9"});
        const auto text = read(entry);
        check(autostart_enabled(), "entry enabled");
        check(text.rfind("[Desktop Entry]\n", 0) == 0 &&
                  text.find("Type=Application\n") != std::string::npos,
            "entry header");
        check(text.find("\nExec=\"/opt/glyph shift/glyphshift-overlay\" --hotkey super+F9\n") !=
                  std::string::npos,
            "entry command quoted");
        enable_autostart({"/usr/bin/glyphshift-overlay"});
        check(read(entry).find("\nExec=/usr/bin/glyphshift-overlay\n") != std::string::npos,
            "entry replaced");
        check(!std::filesystem::exists(entry.parent_path() / ".glyphshift.desktop.tmp"),
            "no temporary file left");
        rejected = false;
        try {
            enable_autostart({"glyphshift-overlay"});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "relative program rejected");
        check(disable_autostart() && !autostart_enabled() && !disable_autostart(),
            "entry removed once");

        check(executable_path().is_absolute() &&
                  executable_path().filename() == "glyphshift-platform-tests",
            "own executable path");
        // Without notify-send on PATH, notify must still return promptly.
        setenv("PATH", (path / "empty").c_str(), 1);
        notify("GlyphShift test", "not shown");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::filesystem::remove_all(path);
        return 1;
    }
    std::filesystem::remove_all(path);
    std::cout << "platform tests passed\n";
    return 0;
}
