#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <glyphshift/platform/desktop.hpp>
#include <stdexcept>
#include <string_view>

// Linux desktop integration: XDG autostart entries and freedesktop
// notifications. Only operating-system and desktop-standard interfaces; no
// window system or GPU API.
namespace glyphshift::platform {
std::filesystem::path config_home() {
    if (const char* value = std::getenv("XDG_CONFIG_HOME");
        value && *value && std::filesystem::path(value).is_absolute())
        return value;
    if (const char* home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        return std::filesystem::path(home) / ".config";
    throw std::runtime_error("needs an absolute XDG_CONFIG_HOME or HOME");
}

std::string desktop_exec_argument(const std::string& argument) {
    constexpr std::string_view reserved = " \t\"'\\><~|&;$*?#()`";
    if (argument.find_first_of("\n\r") != std::string::npos)
        throw std::invalid_argument("a desktop entry argument cannot contain a line break");
    const bool quote = argument.empty() || argument.find_first_of(reserved) != std::string::npos;
    std::string quoted = quote ? "\"" : "";
    for (const char c : argument) {
        if (quote && (c == '"' || c == '`' || c == '$' || c == '\\'))
            quoted += '\\';
        quoted += c;
        if (c == '%')
            quoted += '%';  // Field codes start with %.
    }
    if (quote)
        quoted += '"';
    // The key's value is a string, whose own escaping doubles backslashes.
    std::string escaped;
    for (const char c : quoted)
        escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
    return escaped;
}

std::filesystem::path autostart_entry() {
    return config_home() / "autostart" / "glyphshift.desktop";
}

bool autostart_enabled() {
    return std::filesystem::is_regular_file(autostart_entry());
}

void enable_autostart(const std::vector<std::string>& command) {
    if (command.empty() || !std::filesystem::path(command[0]).is_absolute())
        throw std::invalid_argument("autostart needs an absolute program path");
    std::string exec;
    for (const auto& argument : command)
        exec += (exec.empty() ? "" : " ") + desktop_exec_argument(argument);
    const auto entry = autostart_entry();
    std::filesystem::create_directories(entry.parent_path());
    const auto temporary = entry.parent_path() / ".glyphshift.desktop.tmp";
    {
        std::ofstream stream(temporary, std::ios::trunc);
        stream << "[Desktop Entry]\n"
                  "Type=Application\n"
                  "Name=GlyphShift\n"
                  "Comment=Glyph art overlay for games; press the hotkey in a game\n"
                  "Exec="
               << exec
               << "\n"
                  "Icon=glyphshift\n"
                  "Terminal=false\n"
                  "X-GNOME-Autostart-enabled=true\n";
        stream.close();
        if (!stream)
            throw std::runtime_error("cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, entry);
}

bool disable_autostart() {
    return std::filesystem::remove(autostart_entry());
}

void notify(const std::string& summary, const std::string& body) {
    // Double fork: the notifier is never a child to reap, and a missing
    // notification service only makes notify-send fail on its own.
    const pid_t child = fork();
    if (child < 0)
        return;
    if (child == 0) {
        if (fork() == 0) {
            const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (null >= 0) {
                dup2(null, STDOUT_FILENO);
                dup2(null, STDERR_FILENO);
            }
            execlp("notify-send",
                "notify-send",
                "--app-name=GlyphShift",
                "--icon=glyphshift",
                summary.c_str(),
                body.c_str(),
                static_cast<char*>(nullptr));
        }
        _exit(0);
    }
    waitpid(child, nullptr, 0);
}
}  // namespace glyphshift::platform
