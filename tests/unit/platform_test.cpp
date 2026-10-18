#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/fonts.hpp>
#include <glyphshift/platform/paths.hpp>
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
        // A missing family is not replaced by a substitute: the system default
        // monospace font answers, as with no preferred family at all.
        constexpr std::string_view missing[] = {"GlyphShift No Such Family"};
        const auto fallback = bold_monospace_font({});
        check(std::filesystem::is_regular_file(fallback), "default monospace font file");
        check(bold_monospace_font(missing) == fallback, "missing family falls back to monospace");
        // Children: stopped on request and reported once ended; started
        // children end with their parent even when it is killed.
        const auto ended = [](ProcessId child) {
            for (int i = 0; i < 500; ++i) {
                for (const auto id : ended_children())
                    if (id == child)
                        return true;
                usleep(10000);
            }
            return false;
        };
        const auto sleeper = spawn_child({"/usr/bin/sleep", "30"});
        check(sleeper > 0, "child started");
        stop_child(sleeper);
        check(ended(sleeper), "stopped child reported as ended");
        const auto unknown = spawn_child({(path / "no-such-program").string()});
        check(unknown > 0 && ended(unknown), "failed exec ends the child");
        check(spawn_child({}) == -1, "empty command refused");
        int ids[2];
        check(pipe(ids) == 0, "pipe");
        const pid_t parent = fork();
        if (parent == 0) {
            const ProcessId orphan = spawn_child({"/usr/bin/sleep", "30"});
            const auto written = write(ids[1], &orphan, sizeof orphan);
            (void)written;
            pause();
            _exit(0);
        }
        ProcessId orphan = 0;
        check(read(ids[0], &orphan, sizeof orphan) == sizeof orphan && orphan > 0,
            "grandchild started");
        close(ids[0]);
        close(ids[1]);
        kill(parent, SIGKILL);
        waitpid(parent, nullptr, 0);
        bool gone = false;
        for (int i = 0; i < 500 && !gone; ++i) {
            std::string state = read("/proc/" + std::to_string(orphan) + "/stat");
            const auto close_paren = state.rfind(')');
            gone = state.empty() ||
                   (close_paren != std::string::npos && state.size() > close_paren + 2 &&
                       state[close_paren + 2] == 'Z');
            if (!gone)
                usleep(10000);
        }
        check(gone, "child ends when its parent is killed");
        check(!stop_requested(), "no stop request yet");
        catch_stop_requests();
        raise(SIGTERM);
        check(stop_requested(), "SIGTERM becomes a stop request");
        check(!stderr_is_terminal() || isatty(STDERR_FILENO), "terminal check");
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
