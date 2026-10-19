#include <windows.h>
// After windows.h:
#include <sddl.h>

#include <atomic>
#include <glyphshift/platform/files.hpp>
#include <memory>
#include <mutex>

#include "text.hpp"

// File access, Windows. Win32 has no openat, so files are opened by full path
// after the directory itself was checked; FILE_FLAG_OPEN_REPARSE_POINT opens a
// symbolic link or junction itself, which is then refused instead of followed.
namespace glyphshift::platform {
namespace {
struct Handle {
    HANDLE value;

    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};

constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

bool missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

// Security for new private files: full access for the current user only, not
// inherited from the directory (the equivalent of mode 0600).
SECURITY_ATTRIBUTES* private_security() {
    static std::once_flag once;
    static PSECURITY_DESCRIPTOR descriptor = nullptr;
    static SECURITY_ATTRIBUTES attributes{};
    std::call_once(once, [] {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return;
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        auto buffer = std::make_unique<unsigned char[]>(size);
        wchar_t* sid = nullptr;
        if (GetTokenInformation(token, TokenUser, buffer.get(), size, &size) &&
            ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.get())->User.Sid, &sid)) {
            const std::wstring sddl = L"D:P(A;;FA;;;" + std::wstring(sid) + L")";
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
                    SDDL_REVISION_1,
                    &descriptor,
                    nullptr))
                attributes = {sizeof attributes, descriptor, FALSE};
            LocalFree(sid);
        }
        CloseHandle(token);
    });
    return descriptor ? &attributes : nullptr;
}

// Reads at most limit + 1 bytes; false on a read error.
bool read_all(HANDLE file, std::size_t limit, std::string& text) {
    std::string buffer(limit + 1, '\0');
    std::size_t count = 0;
    while (count < buffer.size()) {
        DWORD n = 0;
        if (!ReadFile(file,
                buffer.data() + count,
                static_cast<DWORD>(buffer.size() - count),
                &n,
                nullptr))
            return false;
        if (!n)
            break;
        count += n;
    }
    buffer.resize(count);
    text = std::move(buffer);
    return true;
}
}  // namespace

PrivateDirectory::PrivateDirectory(const std::filesystem::path& path) : path_(path) {
    HANDLE directory = CreateFileW(path.c_str(),
        GENERIC_READ,
        share_all,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        status_ = missing(error) ? Status::missing : Status::failed;
        system_error_ = windows::message(error);
        return;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(directory, &info)) {
        system_error_ = windows::message(GetLastError());
        CloseHandle(directory);
        return;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        system_error_ = "the directory is a symbolic link or junction";
        CloseHandle(directory);
        return;
    }
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        system_error_ = "not a directory";
        CloseHandle(directory);
        return;
    }
    // Kept open so the directory cannot be deleted or renamed meanwhile.
    handle_ = reinterpret_cast<std::intptr_t>(directory);
    status_ = Status::ok;
}

PrivateDirectory::~PrivateDirectory() {
    if (handle_ != -1)
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
}

PrivateDirectory::Status PrivateDirectory::read(const std::string& name,
    std::size_t limit,
    std::string& text) {
    const Handle file{CreateFileW((path_ / name).c_str(),
        GENERIC_READ,
        share_all,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (missing(error))
            return Status::missing;
        system_error_ = windows::message(error);
        return Status::failed;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info)) {
        system_error_ = windows::message(GetLastError());
        return Status::failed;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        system_error_ = "a symbolic link is not followed";
        return Status::failed;
    }
    const auto size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        GetFileType(file.value) != FILE_TYPE_DISK || size > limit)
        return Status::invalid;
    std::string buffer;
    if (!read_all(file.value, limit, buffer)) {
        system_error_ = windows::message(GetLastError());
        return Status::failed;
    }
    if (buffer.size() > limit)
        return Status::too_large;
    text = std::move(buffer);
    return Status::ok;
}

PrivateDirectory::Write PrivateDirectory::replace(const std::string& name, std::string_view text) {
    static std::atomic<unsigned> serial{0};
    const auto temporary = path_ / (".tmp-" + std::to_string(GetCurrentProcessId()) + "-" +
                                       std::to_string(serial.fetch_add(1)));
    SECURITY_ATTRIBUTES* security = private_security();
    if (!security) {
        system_error_ = "cannot prepare owner-only file permissions";
        return Write::temporary_failed;
    }
    HANDLE file = CreateFileW(temporary.c_str(),
        GENERIC_WRITE,
        0,
        security,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        system_error_ = windows::message(GetLastError());
        return Write::temporary_failed;
    }
    std::size_t count = 0;
    bool ok = true;
    while (ok && count < text.size()) {
        DWORD n = 0;
        ok = WriteFile(file,
                 text.data() + count,
                 static_cast<DWORD>(text.size() - count),
                 &n,
                 nullptr) &&
             n > 0;
        count += n;
    }
    if (ok)
        ok = FlushFileBuffers(file);
    DWORD error = ok ? 0 : GetLastError();
    CloseHandle(file);
    // Renaming over the target replaces a symbolic link there, not its target.
    if (ok && !MoveFileExW(temporary.c_str(),
                  (path_ / name).c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        error = GetLastError();
    }
    if (!ok) {
        system_error_ = windows::message(error);
        DeleteFileW(temporary.c_str());
        return Write::commit_failed;
    }
    return Write::ok;
}

FileStamp file_stamp(const std::filesystem::path& path) noexcept {
    FileStamp stamp;
    const Handle file{CreateFileW(path.c_str(),
        FILE_READ_ATTRIBUTES,
        share_all,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info)) {
        stamp.error = static_cast<int>(GetLastError());
        if (!stamp.error)
            stamp.error = -2;
        return stamp;
    }
    stamp.device = info.dwVolumeSerialNumber;
    stamp.file = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    stamp.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    stamp.modified = static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
        info.ftLastWriteTime.dwLowDateTime);
    stamp.regular = !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    GetFileType(file.value) == FILE_TYPE_DISK;
    stamp.error = 0;
    return stamp;
}

ReadStatus read_regular_file(const std::filesystem::path& path,
    std::size_t limit,
    std::string& text) {
    const Handle file{CreateFileW(path.c_str(),
        GENERIC_READ,
        share_all,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    if (file.value == INVALID_HANDLE_VALUE)
        return ReadStatus::open_failed;
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(file.value) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return ReadStatus::failed;
    std::string buffer;
    if (!read_all(file.value, limit, buffer) || buffer.size() > limit)
        return ReadStatus::failed;
    text = std::move(buffer);
    return ReadStatus::ok;
}

void write_stderr(std::string_view text) noexcept {
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (error && error != INVALID_HANDLE_VALUE)
        WriteFile(error, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}
}  // namespace glyphshift::platform
