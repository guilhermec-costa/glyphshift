#ifdef _WIN32
#include <windows.h>
// After windows.h:
#include <aclapi.h>
#else
#include <sys/stat.h>
#endif

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <glyphshift/platform/files.hpp>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "test_support.hpp"

// File access of the platform layer (Linux and Windows): the private preset
// directory, the live config's stamp and reads, and unbuffered diagnostics.
namespace {
using namespace glyphshift::platform;
using Status = PrivateDirectory::Status;

void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}

void write_file(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

std::string read_file(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Mode 0600 on Linux; on Windows a protected access list (nothing inherited)
// with one entry, allowing the current user.
bool private_file(const std::filesystem::path& file) {
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::string user(size, '\0');
    const bool known = GetTokenInformation(token, TokenUser, user.data(), size, &size);
    CloseHandle(token);
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!known || GetNamedSecurityInfoW(file.c_str(),
                      SE_FILE_OBJECT,
                      DACL_SECURITY_INFORMATION,
                      nullptr,
                      nullptr,
                      &dacl,
                      nullptr,
                      &descriptor) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    void* ace = nullptr;
    const bool ok = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                    (control & SE_DACL_PROTECTED) && dacl && dacl->AceCount == 1 &&
                    GetAce(dacl, 0, &ace) &&
                    static_cast<ACE_HEADER*>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                    EqualSid(&static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart,
                        reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
    LocalFree(descriptor);
    return ok;
#else
    struct stat info{};
    return stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600;
#endif
}
}  // namespace

int main(int argc, char** argv) {
    // Child mode: one diagnostic for the parent to find on standard error.
    if (argc == 2 && std::string(argv[1]) == "--write-stderr") {
        write_stderr("glyphshift: platform-files diagnostic\n");
        return 0;
    }
    const auto root = glyphshift::test::temporary_directory("glyphshift-platform-files");
    try {
        // PrivateDirectory: missing, opened, reads and atomic replacement.
        check(PrivateDirectory(root / "absent").status() == Status::missing,
            "missing directory reported as missing");
        PrivateDirectory directory(root);
        check(directory.status() == Status::ok, "existing directory opened");
        std::string text = "unchanged";
        check(directory.read("absent.conf", 4096, text) == Status::missing && text == "unchanged",
            "missing file reported as missing");
        check(directory.replace("night.conf", "cell_width=21\n") == PrivateDirectory::Write::ok,
            "file replaced");
        check(directory.read("night.conf", 4096, text) == Status::ok && text == "cell_width=21\n",
            "replaced contents read back");
        check(private_file(root / "night.conf"), "replaced file readable by the user only");
        check(
            directory.replace("night.conf", "glyph_strength=1\n") == PrivateDirectory::Write::ok &&
                read_file(root / "night.conf") == "glyph_strength=1\n",
            "existing file replaced");
        write_file(root / "limit.conf", std::string(16, 'x'));
        check(directory.read("limit.conf", 16, text) == Status::ok && text.size() == 16,
            "file at the limit read");
        check(directory.read("limit.conf", 15, text) == Status::invalid,
            "file over the limit refused");
        std::filesystem::create_directory(root / "folder.conf");
        check(directory.read("folder.conf", 4096, text) == Status::invalid,
            "directory refused as a file");
        for (const auto& entry : std::filesystem::directory_iterator(root))
            check(entry.path().filename().string().rfind(".tmp-", 0) != 0,
                "no temporary file left");

        // Symbolic links are refused, never followed; a replacement replaces
        // the link itself. Windows needs Developer Mode or the privilege.
        std::error_code link_error;
        write_file(root / "external", "outside\n");
        std::filesystem::create_symlink(root / "external", root / "link.conf", link_error);
#ifndef _WIN32
        check(!link_error, "symbolic link created for the test");
#endif
        if (!link_error) {
            check(directory.read("link.conf", 4096, text) == Status::failed &&
                      !directory.system_error().empty(),
                "symbolic link to a file refused");
            check(directory.replace("link.conf", "replaced\n") == PrivateDirectory::Write::ok &&
                      read_file(root / "external") == "outside\n" &&
                      !std::filesystem::is_symlink(root / "link.conf") &&
                      read_file(root / "link.conf") == "replaced\n",
                "replacement replaces the link, not its target");
            std::filesystem::create_directory(root / "real");
            std::filesystem::create_directory_symlink(root / "real", root / "linked");
            check(PrivateDirectory(root / "linked").status() == Status::failed,
                "symbolic link to a directory refused");
        } else {
            std::cout << "SKIPPED: symbolic link checks (" << link_error.message() << ")\n";
        }

        // FileStamp: missing, regular, a directory, and an atomic replacement
        // with the same size and modification time.
        const auto config = root / "live.conf";
        check(file_stamp(config).error != 0, "missing file has an error stamp");
        write_file(config, "enabled=true\n");
        const auto first = file_stamp(config);
        check(!first.error && first.regular && first.size == 13, "regular file stamp");
        check(file_stamp(config) == first, "unchanged file keeps its stamp");
        check(!file_stamp(root / "folder.conf").error && !file_stamp(root / "folder.conf").regular,
            "directory is not regular");
        const auto modified = std::filesystem::last_write_time(config);
        write_file(root / "live.new", "enabled=fals\n");
        std::filesystem::last_write_time(root / "live.new", modified);
        std::filesystem::rename(root / "live.new", config);
        const auto replaced = file_stamp(config);
        check(replaced.size == first.size && replaced.modified == first.modified &&
                  !(replaced == first),
            "atomic replacement with equal size and time changes the stamp");

        // read_regular_file: contents, size limit, missing file.
        check(read_regular_file(config, 4096, text) == ReadStatus::ok && text == "enabled=fals\n",
            "live config read");
        check(read_regular_file(config, 12, text) == ReadStatus::failed,
            "live config over the limit refused");
        check(read_regular_file(root / "absent.conf", 4096, text) == ReadStatus::open_failed,
            "missing live config cannot be opened");
        check(read_regular_file(root / "folder.conf", 4096, text) != ReadStatus::ok,
            "directory is not read as a config");

        // write_stderr reaches the process's standard error.
        const auto log = root / "stderr.log";
        const std::string command =
            "\"\"" + std::string(argv[0]) + "\" --write-stderr 2>\"" + log.string() + "\"\"";
#ifdef _WIN32
        check(std::system(command.c_str()) == 0, "child wrote its diagnostic");
#else
        check(std::system(command.substr(1, command.size() - 2).c_str()) == 0,
            "child wrote its diagnostic");
#endif
        check(read_file(log) == "glyphshift: platform-files diagnostic\n", "diagnostic on stderr");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
    std::cout << "Platform files: private directory, links refused, stamps, live reads and "
                 "stderr passed\n";
    return 0;
}
