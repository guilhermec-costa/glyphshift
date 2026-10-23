#include <windows.h>
// After windows.h:
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <glyphshift/platform/desktop.hpp>
#include <glyphshift/platform/fonts.hpp>
#include <glyphshift/platform/paths.hpp>
#include <glyphshift/platform/process.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/platform/windows/command_line.hpp"
#include "test_support.hpp"

// The Windows platform layer's paths, own executable, fonts, start at sign-in
// and processes: the counterpart of the Linux platform test. Child processes
// are this test started in a helper mode.
namespace {
void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

std::wstring wide(const std::string& text) {
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size);
    return result;
}

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

// The font file's name, lower case, from discovery's UTF-8 path.
std::wstring file_name(const std::string& path) {
    return lower(std::filesystem::path(std::u8string(path.begin(), path.end())).filename());
}

std::filesystem::path roaming() {
    PWSTR folder = nullptr;
    const bool found =
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder) == S_OK;
    std::filesystem::path path = found ? folder : L"";
    CoTaskMemFree(folder);
    check(found, "roaming application data folder");
    return path;
}

constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring read_run_value(const std::wstring& name) {
    wchar_t value[1024]{};
    DWORD size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER,
            run_key,
            name.c_str(),
            RRF_RT_REG_SZ,
            nullptr,
            value,
            &size) != ERROR_SUCCESS)
        return L"<missing>";
    return value;
}

template <class Condition>
bool eventually(Condition condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition())
            return true;
        Sleep(20);
    }
    return false;
}

std::wstring stop_event(DWORD process) {
    return L"Local\\GlyphShiftStop-" + std::to_wstring(process);
}

bool listening(DWORD process) {
    const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, stop_event(process).c_str());
    if (event)
        CloseHandle(event);
    return event != nullptr;
}

bool ended(DWORD process) {
    const HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, process);
    if (!handle)
        return true;
    const bool done = WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
    CloseHandle(handle);
    return done;
}

// Helper modes of this executable.
int helper(std::string_view mode, const char* argument) {
    using namespace glyphshift::platform;
    if (mode == "--wait-for-stop") {
        catch_stop_requests();
        for (int i = 0; i < 1500 && !stop_requested(); ++i)
            Sleep(20);
        return stop_requested() ? 0 : 3;
    }
    if (mode == "--ignore-stop") {
        // Listens, but hangs instead of stopping.
        catch_stop_requests();
        Sleep(30000);
        return 0;
    }
    if (mode == "--sleep") {
        Sleep(30000);
        return 0;
    }
    if (mode == "--spawn-sleeper") {
        const auto child = spawn_child({utf8(executable_path()), "--sleep"});
        std::ofstream(argument) << child << '\n';
        Sleep(30000);
        return 0;
    }
    return 2;
}
}  // namespace

int main(int argc, char** argv) {
    using namespace glyphshift::platform;
    namespace test = glyphshift::test;
    if (argc >= 2 && std::string_view(argv[1]).starts_with("--"))
        return helper(argv[1], argc > 2 ? argv[2] : "");
    const auto root = test::temporary_directory("glyphshift-platform-windows");
    try {
        // config_home: an absolute APPDATA, else the roaming folder.
        test::set_env("APPDATA", (root / "config").string());
        check(config_home() == root / "config", "absolute APPDATA used");
        test::set_env("APPDATA", "relative\\config");
        check(config_home() == roaming(), "relative APPDATA ignored");
        test::unset_env("APPDATA");
        check(config_home() == roaming(), "roaming folder without APPDATA");

        const auto self = executable_path();
        check(self.is_absolute() && std::filesystem::is_regular_file(self) &&
                  lower(self.filename()) == L"glyphshift-platform-windows-tests.exe",
            "own executable path");

        // Font discovery. A missing family is not replaced by a substitute:
        // the system's monospace (Consolas Bold) answers, as with no family.
        constexpr std::string_view missing[] = {"GlyphShift No Such Family"};
        const auto fallback = bold_monospace_font({});
        check(std::filesystem::is_regular_file(
                  std::filesystem::path(std::u8string(fallback.begin(), fallback.end()))),
            "default monospace font file");
        check(file_name(fallback) == L"consolab.ttf", "default is Consolas Bold");
        check(bold_monospace_font(missing) == fallback, "missing family falls back to monospace");
        // An installed family with a real bold face is used in bold.
        constexpr std::string_view courier[] = {"GlyphShift No Such Family", "Courier New"};
        check(file_name(bold_monospace_font(courier)) == L"courbd.ttf",
            "first installed family in bold");
        // Lucida Console has no bold face: skipped rather than drawn thin.
        constexpr std::string_view lucida[] = {"Lucida Console"};
        check(bold_monospace_font(lucida) == fallback, "family without bold skipped");
        // Cascadia Mono ships as a variable font: its file's default instance
        // is regular, so it is skipped too.
        constexpr std::string_view cascadia[] = {"Cascadia Mono"};
        if (std::filesystem::exists(L"C:\\Windows\\Fonts\\CascadiaMono.ttf"))
            check(bold_monospace_font(cascadia) == fallback, "variable font skipped");
        else
            std::cout << "SKIPPED: variable font check (Cascadia Mono not installed)\n";

        // Command lines parse back to the same arguments.
        const std::vector<std::string> arguments{"C:\\Program Files\\glyph shift\\a.exe",
            "plain",
            "two words",
            "quote\"inside",
            "trailing\\",
            "slash\\\"quote",
            "",
            "tab\there",
            "\xc3\xa9t\xc3\xa9 \xe2\x98\x83"};
        const auto line = glyphshift::platform::windows::command_line(arguments);
        int count = 0;
        wchar_t** parsed = CommandLineToArgvW(line.c_str(), &count);
        check(parsed && count == static_cast<int>(arguments.size()), "argument count");
        for (int i = 0; i < count; ++i)
            check(wide(arguments[static_cast<std::size_t>(i)]) == parsed[i],
                "argument parsed back unchanged");
        LocalFree(parsed);

        // Start at sign-in: the Run value, under a name of this test only.
        const auto value = L"GlyphShift-test-" + std::to_wstring(GetCurrentProcessId());
        test::set_env("GLYPHSHIFT_AUTOSTART_VALUE",
            "GlyphShift-test-" + std::to_string(GetCurrentProcessId()));
        check(autostart_entry().filename() == value && !autostart_enabled(), "no entry initially");
        check(!disable_autostart(), "removing a missing entry reports false");
        enable_autostart(
            {"C:\\Program Files\\glyph shift\\glyphshift-overlay.exe", "--hotkey", "super+F9"});
        check(autostart_enabled() &&
                  read_run_value(value) ==
                      L"\"C:\\Program Files\\glyph shift\\glyphshift-overlay.exe\" --hotkey "
                      L"super+F9",
            "entry written with the program quoted");
        enable_autostart({"C:\\GlyphShift\\glyphshift-overlay.exe"});
        check(read_run_value(value) == L"C:\\GlyphShift\\glyphshift-overlay.exe", "entry replaced");
        bool rejected = false;
        try {
            enable_autostart({"glyphshift-overlay.exe"});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "relative program rejected");
        check(disable_autostart() && !autostart_enabled() && !disable_autostart() &&
                  read_run_value(value) == L"<missing>",
            "entry removed once");
        test::unset_env("GLYPHSHIFT_AUTOSTART_VALUE");

        // Children: stopped on request through their stop event (a clean
        // exit), ended when they never listen, refused when they cannot start.
        const auto program = utf8(self);
        const auto listener = spawn_child({program, "--wait-for-stop"});
        check(listener > 0 && eventually([&] {
            return listening(static_cast<DWORD>(listener));
        }),
            "child listens for stop requests");
        const HANDLE listener_handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            static_cast<DWORD>(listener));
        stop_child(listener);
        wait_child(listener);
        DWORD code = 99;
        GetExitCodeProcess(listener_handle, &code);
        CloseHandle(listener_handle);
        check(code == 0, "a stop request ends the child cleanly");
        // A child that listens but hangs is ended after the grace period.
        const auto hung = spawn_child({program, "--ignore-stop"});
        check(hung > 0 && eventually([&] {
            return listening(static_cast<DWORD>(hung));
        }),
            "hung child listens for stop requests");
        const auto before = std::chrono::steady_clock::now();
        stop_child(hung);
        wait_child(hung);
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - before)
                                .count();
        check(waited >= child_grace_ms - 200 && waited < child_grace_ms + 3000,
            "a hung child is ended after the grace period");
        const auto sleeper = spawn_child({program, "--sleep"});
        check(sleeper > 0, "child started");
        stop_child(sleeper);
        bool reported = false;
        check(eventually([&] {
            for (const auto id : ended_children())
                reported |= id == sleeper;
            return reported;
        }),
            "stopped child reported as ended");
        check(spawn_child({"C:\\GlyphShift\\no such program.exe"}) == -1,
            "a program that cannot start is refused");
        check(spawn_child({}) == -1, "empty command refused");
        // A child's own children end when it is killed: its job closes.
        const auto pid_file = root / "grandchild.txt";
        const auto parent = spawn_child({program, "--spawn-sleeper", utf8(pid_file)});
        DWORD grandchild = 0;
        check(eventually([&] {
            std::ifstream(pid_file) >> grandchild;
            return grandchild != 0;
        }) && !ended(grandchild),
            "grandchild started");
        const HANDLE killable = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(parent));
        check(killable && TerminateProcess(killable, 9), "parent killed");
        CloseHandle(killable);
        wait_child(parent);
        check(eventually([&] {
            return ended(grandchild);
        }),
            "child ends when its parent is killed");

        // This process's own stop event becomes a stop request.
        catch_stop_requests();
        check(!stop_requested(), "no stop request yet");
        const HANDLE event =
            OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_event(GetCurrentProcessId()).c_str());
        check(event && SetEvent(event), "own stop event");
        CloseHandle(event);
        check(eventually([] {
            return stop_requested();
        }),
            "the stop event becomes a stop request");
        DWORD mode = 0;
        check(!stderr_is_terminal() || GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode),
            "terminal check");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
    std::cout << "Windows platform: configuration folder, own executable, fonts, command lines, "
                 "start at sign-in and processes passed\n";
    return 0;
}
