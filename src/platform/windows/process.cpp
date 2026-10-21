#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <glyphshift/platform/process.hpp>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "command_line.hpp"
#include "text.hpp"

// Process facilities, Windows: the executable's path, children kept in a job
// object that kills them when this process ends (even when it is killed),
// stop requests (the console's Ctrl+C, Ctrl+Break, close, logoff and shutdown,
// or this process's named stop event, which stop_child() signals: the
// counterpart of SIGTERM), the terminal check and environment variables.
namespace glyphshift::platform {
namespace {
std::atomic<bool> stopping{false};

BOOL WINAPI console_event(DWORD) {
    stopping = true;
    return TRUE;
}

std::wstring stop_event_name(DWORD process) {
    return L"Local\\GlyphShiftStop-" + std::to_wstring(process);
}

// One argument as the Microsoft C runtime parses it back.
std::wstring quoted(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // Backslashes before a quote are doubled, and the quote escaped.
        result.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        result += c;
    }
    // Backslashes before the closing quote are doubled too.
    result.append(backslashes * 2, L'\\');
    return result + L"\"";
}

// Children end with this process: the job closes when its last handle does.
HANDLE children_job() {
    static const HANDLE job = [] {
        const HANDLE created = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (created)
            SetInformationJobObject(created,
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof limits);
        return created;
    }();
    return job;
}

struct Children {
    std::mutex mutex;
    std::map<ProcessId, HANDLE> handles;
};

Children& children() {
    static Children value;
    return value;
}
}  // namespace

namespace windows {
std::wstring command_line(const std::vector<std::string>& argv) {
    std::wstring line;
    for (const auto& argument : argv) {
        if (!line.empty())
            line += L' ';
        line += quoted(wide(argument));
    }
    return line;
}
}  // namespace windows

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

ProcessId spawn_child(const std::vector<std::string>& argv) {
    if (argv.empty())
        return -1;
    const auto program = windows::wide(argv[0]);
    auto line = windows::command_line(argv);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    // Suspended until it belongs to the job, so it cannot outlive us. Children
    // share this process's console, or get none (never a console window).
    const DWORD flags = CREATE_SUSPENDED | (GetConsoleWindow() ? 0 : CREATE_NO_WINDOW);
    if (!CreateProcessW(program.c_str(),
            line.data(),
            nullptr,
            nullptr,
            FALSE,
            flags,
            nullptr,
            nullptr,
            &startup,
            &process))
        return -1;
    const HANDLE job = children_job();
    if (!job || !AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return -1;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    const auto id = static_cast<ProcessId>(process.dwProcessId);
    auto& list = children();
    const std::lock_guard lock(list.mutex);
    list.handles[id] = process.hProcess;
    return id;
}

void stop_child(ProcessId child) noexcept {
    const HANDLE event =
        OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_event_name(static_cast<DWORD>(child)).c_str());
    if (event) {
        SetEvent(event);
        CloseHandle(event);
        return;
    }
    // Not listening yet (or never): end it, as SIGTERM's default would.
    auto& list = children();
    const std::lock_guard lock(list.mutex);
    if (const auto found = list.handles.find(child); found != list.handles.end())
        TerminateProcess(found->second, 1);
}

void wait_child(ProcessId child) noexcept {
    HANDLE handle = nullptr;
    {
        auto& list = children();
        const std::lock_guard lock(list.mutex);
        const auto found = list.handles.find(child);
        if (found == list.handles.end())
            return;
        handle = found->second;
        list.handles.erase(found);
    }
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
}

std::vector<ProcessId> ended_children() {
    std::vector<ProcessId> ended;
    auto& list = children();
    const std::lock_guard lock(list.mutex);
    for (auto it = list.handles.begin(); it != list.handles.end();) {
        if (WaitForSingleObject(it->second, 0) == WAIT_OBJECT_0) {
            ended.push_back(it->first);
            CloseHandle(it->second);
            it = list.handles.erase(it);
        } else {
            ++it;
        }
    }
    return ended;
}

void catch_stop_requests() {
    SetConsoleCtrlHandler(console_event, TRUE);
    static std::once_flag once;
    std::call_once(once, [] {
        const HANDLE event =
            CreateEventW(nullptr, TRUE, FALSE, stop_event_name(GetCurrentProcessId()).c_str());
        if (!event)
            return;
        std::thread([event] {
            WaitForSingleObject(event, INFINITE);
            stopping = true;
        }).detach();
    });
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
