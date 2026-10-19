#include <shlobj.h>
#include <windows.h>

#include <cstdlib>
#include <glyphshift/platform/paths.hpp>
#include <stdexcept>

namespace glyphshift::platform {
std::filesystem::path config_home() {
    // An absolute APPDATA overrides the folder, as XDG_CONFIG_HOME does on Linux.
    if (const wchar_t* value = _wgetenv(L"APPDATA");
        value && *value && std::filesystem::path(value).is_absolute())
        return value;
    PWSTR folder = nullptr;
    const bool found =
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder) == S_OK;
    std::filesystem::path path = found ? std::filesystem::path(folder) : std::filesystem::path();
    CoTaskMemFree(folder);
    if (!found || !path.is_absolute())
        throw std::runtime_error("needs an absolute APPDATA or a roaming application data folder");
    return path;
}
}  // namespace glyphshift::platform
