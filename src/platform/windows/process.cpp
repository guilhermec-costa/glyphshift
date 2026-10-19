#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <glyphshift/platform/process.hpp>
#include <stdexcept>
#include <string>

// Process facilities, Windows: the executable's path, stop requests from the
// console (Ctrl+C, Ctrl+Break, closing it, logoff and shutdown), the terminal
// check and environment variables. Spawning and stopping overlays follow with
// the background daemon.
namespace glyphshift::platform {
namespace {
std::atomic<bool> stopping{false};

BOOL WINAPI console_event(DWORD) {
    stopping = true;
    return TRUE;
}
}  // namespace

std::filesystem::path executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length)
            throw std::runtime_error("cannot locate the running executable");
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

void catch_stop_requests() {
    SetConsoleCtrlHandler(console_event, TRUE);
}

bool stop_requested() noexcept {
    return stopping.load();
}

void request_stop() noexcept {
    stopping = true;
}

bool stderr_is_terminal() noexcept {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode) != 0;
}

void set_environment(const char* name, const std::string& value) {
    _putenv_s(name, value.c_str());
}
}  // namespace glyphshift::platform
