#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <glyphshift/platform/files.hpp>

namespace glyphshift::platform {
namespace {
struct File {
    int fd;

    ~File() {
        if (fd >= 0)
            close(fd);
    }
};
}  // namespace

PrivateDirectory::PrivateDirectory(const std::filesystem::path& path) : path_(path) {
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        status_ = errno == ENOENT ? Status::missing : Status::failed;
        system_error_ = std::strerror(errno);
        return;
    }
    handle_ = fd;
    status_ = Status::ok;
}

PrivateDirectory::~PrivateDirectory() {
    if (handle_ >= 0)
        close(static_cast<int>(handle_));
}

PrivateDirectory::Status PrivateDirectory::read(const std::string& name,
    std::size_t limit,
    std::string& text) {
    const int dir = static_cast<int>(handle_);
    const File file{openat(dir, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (file.fd < 0) {
        if (errno == ENOENT)
            return Status::missing;
        system_error_ = std::strerror(errno);
        return Status::failed;
    }
    struct stat info{};
    if (fstat(file.fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        static_cast<std::size_t>(info.st_size) > limit)
        return Status::invalid;
    std::string buffer(limit + 1, '\0');
    std::size_t count = 0;
    while (count < buffer.size()) {
        const auto n = ::read(file.fd, buffer.data() + count, buffer.size() - count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            system_error_ = std::strerror(errno);
            return Status::failed;
        }
        if (!n)
            break;
        count += static_cast<std::size_t>(n);
    }
    if (count > limit)
        return Status::too_large;
    text.assign(buffer.data(), count);
    return Status::ok;
}

PrivateDirectory::Write PrivateDirectory::replace(const std::string& name, std::string_view text) {
    static std::atomic<unsigned> serial{0};
    const int dir = static_cast<int>(handle_);
    const std::string temporary =
        ".tmp-" + std::to_string(getpid()) + "-" + std::to_string(serial.fetch_add(1));
    const File file{
        openat(dir, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (file.fd < 0) {
        system_error_ = std::strerror(errno);
        return Write::temporary_failed;
    }
    std::size_t count = 0;
    bool ok = true;
    while (count < text.size()) {
        const auto n = write(file.fd, text.data() + count, text.size() - count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        count += static_cast<std::size_t>(n);
    }
    if (ok)
        ok = fsync(file.fd) == 0;
    if (ok)
        ok = renameat(dir, temporary.c_str(), dir, name.c_str()) == 0;
    if (ok)
        ok = fsync(dir) == 0;
    if (!ok) {
        system_error_ = std::strerror(errno);
        unlinkat(dir, temporary.c_str(), 0);
        return Write::commit_failed;
    }
    return Write::ok;
}

FileStamp file_stamp(const std::filesystem::path& path) noexcept {
    FileStamp stamp;
    struct stat status{};
    if (stat(path.c_str(), &status) != 0) {
        stamp.error = errno;
        return stamp;
    }
    stamp.device = static_cast<std::uint64_t>(status.st_dev);
    stamp.file = static_cast<std::uint64_t>(status.st_ino);
    stamp.size = static_cast<std::uint64_t>(status.st_size);
    stamp.modified = static_cast<std::int64_t>(status.st_mtim.tv_sec) * 1000000000 +
                     static_cast<std::int64_t>(status.st_mtim.tv_nsec);
    stamp.regular = S_ISREG(status.st_mode);
    stamp.error = 0;
    return stamp;
}

ReadStatus read_regular_file(const std::filesystem::path& path,
    std::size_t limit,
    std::string& text) {
    const File file{open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)};
    if (file.fd < 0)
        return ReadStatus::open_failed;
    struct stat opened{};
    if (fstat(file.fd, &opened) != 0 || !S_ISREG(opened.st_mode))
        return ReadStatus::failed;
    std::string buffer(limit + 1, '\0');
    std::size_t count = 0;
    while (count < buffer.size()) {
        const auto n = ::read(file.fd, buffer.data() + count, buffer.size() - count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return ReadStatus::failed;
        if (!n)
            break;
        count += static_cast<std::size_t>(n);
    }
    if (count > limit)
        return ReadStatus::failed;
    text.assign(buffer.data(), count);
    return ReadStatus::ok;
}

void write_stderr(std::string_view text) noexcept {
    const auto ignored = write(STDERR_FILENO, text.data(), text.size());
    (void)ignored;
}
}  // namespace glyphshift::platform
