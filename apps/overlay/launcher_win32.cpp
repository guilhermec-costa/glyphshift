#include <windows.h>

#include <string>

#include "background_win32.hpp"

// GlyphShift.exe: the program the Start menu, Explorer and start at sign-in
// run. glyphshift-overlay is a console program, which Windows would give a
// console window; this one has none and starts it without a console, with
// the same arguments, so only the notification-area icon and notifications
// show that GlyphShift runs. A terminal keeps running glyphshift-overlay
// itself to read its messages.
namespace {
// The arguments of this process's command line, without the program name.
std::wstring arguments() {
    const wchar_t* line = GetCommandLineW();
    if (*line == L'"') {
        for (++line; *line && *line != L'"'; ++line) {
        }
        if (*line)
            ++line;
    } else {
        while (*line && *line != L' ' && *line != L'\t')
            ++line;
    }
    while (*line == L' ' || *line == L'\t')
        ++line;
    return line;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto program = glyphshift::overlay::sibling(L"glyphshift-overlay.exe");
    std::wstring command = L"\"" + program.wstring() + L"\"";
    if (const auto rest = arguments(); !rest.empty())
        command += L" " + rest;
    STARTUPINFOW startup{sizeof startup};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        const std::wstring message = L"GlyphShift could not start " + program.wstring() +
                                     L" (error " + std::to_wstring(GetLastError()) + L").";
        MessageBoxW(nullptr, message.c_str(), L"GlyphShift", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
