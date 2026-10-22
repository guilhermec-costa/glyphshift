#pragma once
#include <windows.h>

#include <filesystem>
#include <string>

// The background launcher on Windows, GlyphShift.exe, next to
// glyphshift-overlay.exe: start at sign-in runs it, so no console window
// opens.
namespace glyphshift::overlay {
// A file in this program's directory.
inline std::filesystem::path sibling(const wchar_t* name) {
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    return std::filesystem::path(path).replace_filename(name);
}

// The program start at sign-in runs: GlyphShift.exe when it is installed next
// to this one, else this program (`program`, UTF-8).
inline std::string background_program(const std::string& program) {
    const auto launcher = sibling(L"GlyphShift.exe");
    if (!std::filesystem::is_regular_file(launcher))
        return program;
    const auto path = launcher.u8string();
    return std::string(path.begin(), path.end());
}
}  // namespace glyphshift::overlay
