#include <windows.h>

#include <glyphshift/platform/process.hpp>
#include <stdexcept>
#include <string>

// Process facilities, Windows. Only the executable's path so far; spawning
// and stopping overlays follow with the background daemon.
namespace glyphshift::platform {
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
}  // namespace glyphshift::platform
