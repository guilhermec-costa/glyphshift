#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Process facilities of the platform layer (Linux: /proc, fork/exec, signals;
// Windows: module paths and console control events).
namespace glyphshift::platform {
// Absolute path of the running executable, symbolic links resolved.
std::filesystem::path executable_path();

using ProcessId = long;

// Starts `argv` (argv[0]: an absolute program path) as a child process that
// is asked to stop when this process ends, even if this one is killed.
// Returns its id, or -1 when it cannot be started.
ProcessId spawn_child(const std::vector<std::string>& argv);

// Asks a child to stop (Linux: SIGTERM); it is not waited for.
void stop_child(ProcessId child) noexcept;

// Waits until a child has ended.
void wait_child(ProcessId child) noexcept;

// Children that have ended since the last call, without waiting.
std::vector<ProcessId> ended_children();

// After this call, interrupt and termination requests (Linux: SIGINT and
// SIGTERM) no longer end the process; stop_requested() reports them.
void catch_stop_requests();
bool stop_requested() noexcept;
void request_stop() noexcept;

// True when diagnostics on stderr reach a terminal the user can read.
bool stderr_is_terminal() noexcept;

// Sets an environment variable of this process (and its later children).
void set_environment(const char* name, const std::string& value);
}  // namespace glyphshift::platform
