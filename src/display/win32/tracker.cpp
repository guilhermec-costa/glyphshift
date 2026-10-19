#include "tracker.hpp"

#include <dwmapi.h>

#include <filesystem>

namespace glyphshift::display::win32 {
Tracker::State Tracker::query() const {
    State state;
    state.exists = IsWindow(target_);
    if (!state.exists)
        return state;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(target_, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    state.viewable = IsWindowVisible(target_) && !IsIconic(target_) && !cloaked;
    RECT client{};
    POINT origin{0, 0};
    if (GetClientRect(target_, &client) && ClientToScreen(target_, &origin))
        state.client = {origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    return state;
}

std::string Tracker::application() const {
    DWORD process_id = 0;
    GetWindowThreadProcessId(target_, &process_id);
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process)
        return {};
    wchar_t path[MAX_PATH * 4];
    DWORD size = static_cast<DWORD>(std::size(path));
    const bool found = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!found)
        return {};
    const auto stem = std::filesystem::path(std::wstring(path, size)).stem().wstring();
    std::string result;
    // Profile names are ASCII; other characters become '-' later anyway.
    for (const wchar_t c : stem)
        result += c < 0x80 ? static_cast<char>(c) : '-';
    return result;
}
}  // namespace glyphshift::display::win32
