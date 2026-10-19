#pragma once
#include <windows.h>

#include <string>
#include <string_view>

// UTF-8 / UTF-16 conversion and system messages for the Windows platform layer.
namespace glyphshift::platform::windows {
inline std::string utf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

inline std::wstring wide(std::string_view text) {
    if (text.empty())
        return {};
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

// The system's description of an error code, without the trailing line break.
inline std::string message(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    std::wstring text(buffer ? buffer : L"", length);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text.empty() ? "error " + std::to_string(code) : utf8(text);
}
}  // namespace glyphshift::platform::windows
