#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <glyphshift/platform/process.hpp>
#include <stdexcept>

namespace glyphshift::platform {
namespace {
volatile std::sig_atomic_t stop = 0;
}  // namespace

std::filesystem::path executable_path() {
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || path.empty())
        throw std::runtime_error("cannot locate the running executable");
    return path;
}

ProcessId spawn_child(const std::vector<std::string>& argv) {
    if (argv.empty())
        return -1;
    // Prepared before fork: the child only calls async-signal-safe functions.
    std::vector<const char*> arguments;
    for (const auto& argument : argv)
        arguments.push_back(argument.c_str());
    arguments.push_back(nullptr);
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child < 0)
        return -1;
    if (child)
        return child;
    // The child ends with its parent, even if the parent is killed.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() != parent)
        _exit(0);
    execv(arguments[0], const_cast<char* const*>(arguments.data()));
    _exit(127);
}

void stop_child(ProcessId child) noexcept {
    if (child > 0)
        kill(static_cast<pid_t>(child), SIGTERM);
}

void wait_child(ProcessId child) noexcept {
    if (child > 0)
        waitpid(static_cast<pid_t>(child), nullptr, 0);
}

std::vector<ProcessId> ended_children() {
    std::vector<ProcessId> ended;
    for (pid_t pid; (pid = waitpid(-1, nullptr, WNOHANG)) > 0;)
        ended.push_back(pid);
    return ended;
}

void catch_stop_requests() {
    struct sigaction action{};
    action.sa_handler = [](int) {
        stop = 1;
    };
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

bool stop_requested() noexcept {
    return stop != 0;
}

void request_stop() noexcept {
    stop = 1;
}

bool stderr_is_terminal() noexcept {
    return isatty(STDERR_FILENO) != 0;
}
}  // namespace glyphshift::platform
