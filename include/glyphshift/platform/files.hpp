#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// File access of the platform layer (Linux: POSIX descriptors; Windows: file
// handles): the private preset directory, the live config file and unbuffered
// diagnostics.
namespace glyphshift::platform {
// One directory of small private files. Neither the directory nor a file is
// ever reached through a symbolic link; replacing a file is atomic and
// durable, and the new file is readable by the user only.
class PrivateDirectory {
public:
    enum class Status { ok, missing, invalid, too_large, failed };

    // Opens an existing directory; status() is missing when there is none.
    explicit PrivateDirectory(const std::filesystem::path& path);
    ~PrivateDirectory();
    PrivateDirectory(const PrivateDirectory&) = delete;
    PrivateDirectory& operator=(const PrivateDirectory&) = delete;

    Status status() const noexcept {
        return status_;
    }

    // The system's description of the last failure.
    const std::string& system_error() const noexcept {
        return system_error_;
    }

    // Reads `name`: missing when it does not exist, invalid when it is not a
    // regular file or is larger than `limit`, too_large when it grew past
    // `limit` while being read.
    Status read(const std::string& name, std::size_t limit, std::string& text);

    enum class Write { ok, temporary_failed, commit_failed };

    // Writes `text` to a new temporary file, flushes it and renames it over
    // `name`; a symbolic link at `name` is replaced, never followed.
    Write replace(const std::string& name, std::string_view text);

private:
    std::filesystem::path path_;
    std::intptr_t handle_ = -1;
    Status status_ = Status::failed;
    std::string system_error_;
};

// Identity and state of a file, compared to notice a change, including an
// atomic replacement that keeps the size and modification time.
struct FileStamp {
    std::uint64_t device = 0, file = 0, size = 0;
    std::int64_t modified = 0;
    bool regular = false;
    int error = -1;

    bool operator==(const FileStamp&) const = default;
};

// The stamp of `path`, following symbolic links; error is nonzero when it
// cannot be read.
FileStamp file_stamp(const std::filesystem::path& path) noexcept;

enum class ReadStatus { ok, open_failed, failed };

// Reads a regular file of at most `limit` bytes, following symbolic links and
// never blocking on special files; failed when it is larger.
ReadStatus read_regular_file(const std::filesystem::path& path,
    std::size_t limit,
    std::string& text);

// Writes a diagnostic to standard error without buffering; never fails the
// caller.
void write_stderr(std::string_view text) noexcept;
}  // namespace glyphshift::platform
