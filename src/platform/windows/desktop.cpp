#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <glyphshift/platform/desktop.hpp>
#include <stdexcept>

#include "command_line.hpp"
#include "text.hpp"

// Desktop integration, Windows: start-on-login through the user's Run key
// (HKCU\Software\Microsoft\Windows\CurrentVersion\Run), which Windows starts
// at sign-in. GLYPHSHIFT_AUTOSTART_VALUE (tests) names another value than
// "GlyphShift". Windows shows notifications only from a notification-area
// icon, which belongs to the window-system layer: the daemon's icon
// (display::win32::Tray) shows them, and notify() does nothing here.
namespace glyphshift::platform {
namespace {
constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring value_name() {
    if (const wchar_t* name = _wgetenv(L"GLYPHSHIFT_AUTOSTART_VALUE"); name && *name)
        return name;
    return L"GlyphShift";
}
}  // namespace

std::filesystem::path autostart_entry() {
    return std::filesystem::path(L"HKEY_CURRENT_USER") / run_key / value_name();
}

bool autostart_enabled() {
    DWORD type = 0;
    return RegGetValueW(HKEY_CURRENT_USER,
               run_key,
               value_name().c_str(),
               RRF_RT_REG_SZ,
               &type,
               nullptr,
               nullptr) == ERROR_SUCCESS;
}

void enable_autostart(const std::vector<std::string>& command) {
    if (command.empty() || !std::filesystem::path(windows::wide(command.front())).is_absolute())
        throw std::invalid_argument("autostart needs an absolute program path");
    const auto line = windows::command_line(command);
    // One value write: Windows replaces it atomically.
    const LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER,
        run_key,
        value_name().c_str(),
        REG_SZ,
        line.c_str(),
        static_cast<DWORD>((line.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS)
        throw std::runtime_error("cannot write the start-at-login entry: " +
                                 windows::message(static_cast<DWORD>(status)));
}

bool disable_autostart() {
    return RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, value_name().c_str()) == ERROR_SUCCESS;
}

void notify(const std::string&, const std::string&) {}
}  // namespace glyphshift::platform
