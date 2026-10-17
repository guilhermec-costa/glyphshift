#include <elf.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glyphshift/config/presets.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

extern char** environ;

namespace {
class File {
 public:
  explicit File(int descriptor) : descriptor_(descriptor) {}
  ~File() {
    if (descriptor_ >= 0) close(descriptor_);
  }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  int get() const { return descriptor_; }

 private:
  int descriptor_;
};

std::runtime_error system_error(const std::string& operation) {
  return std::runtime_error(operation + ": " + std::strerror(errno));
}

void check_launcher_privileges() {
  if (getuid() == 0 || getuid() != geteuid() || getgid() != getegid() ||
      getauxval(AT_SECURE) != 0) {
    throw std::runtime_error(
        "refusing to launch from a privileged process; run as a normal user");
  }
  __user_cap_header_struct header{};
  header.version = _LINUX_CAPABILITY_VERSION_3;
  __user_cap_data_struct caps[2]{};
  if (syscall(SYS_capget, &header, caps) != 0) throw system_error("capget");
  for (const auto& cap : caps) {
    if (cap.effective || cap.permitted || cap.inheritable) {
      throw std::runtime_error(
          "refusing to launch from a process with capabilities");
    }
  }
}

std::string find_target(std::string_view name) {
  if (name.find('/') != std::string_view::npos) return std::string(name);
  const char* path = std::getenv("PATH");
  const std::string_view search = path ? path : "/bin:/usr/bin";
  bool denied = false;
  std::size_t start = 0;
  do {
    const auto end = search.find(':', start);
    const auto directory =
        search.substr(start, end == std::string_view::npos ? end : end - start);
    const auto candidate =
        (directory.empty() ? std::string(".") : std::string(directory)) + "/" +
        std::string(name);
    struct stat status{};
    if (stat(candidate.c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
      if (access(candidate.c_str(), X_OK) == 0) return candidate;
      denied = true;
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  } while (true);
  errno = denied ? EACCES : ENOENT;
  throw system_error("cannot find executable " + std::string(name));
}

void check_target(int fd) {
  struct stat status{};
  if (fstat(fd, &status) != 0) throw system_error("inspect target");
  if (!S_ISREG(status.st_mode))
    throw std::runtime_error("target must be a regular executable file");
  if (status.st_mode & (S_ISUID | S_ISGID))
    throw std::runtime_error("refusing a setuid/setgid target");
  const auto capabilities = fgetxattr(fd, "security.capability", nullptr, 0);
  if (capabilities > 0)
    throw std::runtime_error("refusing a target with file capabilities");
  if (capabilities < 0 && errno != ENODATA && errno != ENOTSUP)
    throw system_error("inspect target capabilities");
  if (faccessat(fd, "", X_OK, AT_EMPTY_PATH | AT_EACCESS) != 0)
    throw system_error("target is not executable");

  Elf64_Ehdr elf{};
  if (pread(fd, &elf, sizeof(elf), 0) != sizeof(elf) ||
      std::memcmp(elf.e_ident, ELFMAG, SELFMAG) != 0 ||
      elf.e_ident[EI_CLASS] != ELFCLASS64 ||
      elf.e_ident[EI_DATA] != ELFDATA2LSB || elf.e_machine != EM_X86_64 ||
      (elf.e_type != ET_EXEC && elf.e_type != ET_DYN) ||
      elf.e_phentsize != sizeof(Elf64_Phdr) || elf.e_phnum > 1024) {
    throw std::runtime_error(
        "GlyphShift requires a dynamically linked Linux x86-64 ELF target");
  }
  bool interpreter = false;
  for (unsigned i = 0; i < elf.e_phnum; ++i) {
    const auto offset =
        elf.e_phoff + static_cast<Elf64_Off>(i) * sizeof(Elf64_Phdr);
    if (offset > static_cast<Elf64_Off>(status.st_size) ||
        sizeof(Elf64_Phdr) > static_cast<Elf64_Off>(status.st_size) - offset)
      throw std::runtime_error("invalid ELF program headers");
    Elf64_Phdr program{};
    if (pread(fd, &program, sizeof(program), static_cast<off_t>(offset)) !=
        sizeof(program))
      throw std::runtime_error("cannot read ELF program headers");
    interpreter = interpreter || program.p_type == PT_INTERP;
  }
  if (!interpreter)
    throw std::runtime_error("static executables cannot use LD_PRELOAD");
}

std::string asset_path(const char* name) {
  // /proc/self/exe works independently of argv[0], PATH and the working
  // directory.
  const auto executable = std::filesystem::read_symlink("/proc/self/exe");
  const auto module = executable.parent_path() / name;
  const auto path = module.string();
  if (path.find_first_of(" :\t\r\n\v\f") != std::string::npos)
    throw std::runtime_error(
        "LD_PRELOAD cannot represent the module path; use a build path without "
        "whitespace or colons");
  struct stat status{};
  if (stat(path.c_str(), &status) != 0)
    throw system_error("cannot find " + path);
  if (!S_ISREG(status.st_mode) || access(path.c_str(), R_OK) != 0)
    throw std::runtime_error("module is not a readable regular file: " + path);
  return path;
}
}  // namespace

int main(int argc, char** argv) {
  constexpr const char* usage =
      "Usage: glyphshift [--profile ID] [--preset NAME] -- PROGRAM [ARGS...]\n";
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << usage
              << "Launch a local/offline Linux x86-64 application with "
                 "GLX/Vulkan glyph rendering.\nPresets are per executable (or "
                 "explicit --profile), under XDG_CONFIG_HOME.\n";
    return 0;
  }
  int target_arg = 1;
  std::string profile, preset;
  while (target_arg < argc && std::string_view(argv[target_arg]) != "--") {
    const std::string_view option = argv[target_arg++];
    if ((option != "--profile" && option != "--preset") || target_arg >= argc ||
        !glyphshift::preset_name_valid(argv[target_arg])) {
      std::cerr << usage;
      return 2;
    }
    (option == "--profile" ? profile : preset) = argv[target_arg++];
  }
  if (target_arg >= argc || ++target_arg >= argc || !*argv[target_arg]) {
    std::cerr << usage;
    return 2;
  }
  int failure = 126;
  try {
    check_launcher_privileges();
    std::string target;
    try {
      target = find_target(argv[target_arg]);
    } catch (...) {
      if (errno == ENOENT) failure = 127;
      throw;
    }
    const File file(open(target.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
    if (file.get() < 0) {
      if (errno == ENOENT) failure = 127;
      throw system_error("open target");
    }
    check_target(file.get());
    if (profile.empty()) {
      if (const char* inherited = std::getenv("GLYPHSHIFT_PROFILE");
          inherited && *inherited)
        profile = inherited;
      else
        profile = glyphshift::profile_id(
                      std::filesystem::read_symlink("/proc/self/fd/" +
                                                    std::to_string(file.get()))
                          .string())
                      .data();
    }
    if (!glyphshift::preset_name_valid(profile))
      throw std::runtime_error("invalid GLYPHSHIFT_PROFILE");
    if (setenv("GLYPHSHIFT_PROFILE", profile.c_str(), 1) != 0)
      throw system_error("set profile identity");
    if (!preset.empty() && setenv("GLYPHSHIFT_PRESET", preset.c_str(), 1) != 0)
      throw system_error("set preset selection");
    std::string preload = asset_path("libglyphshift_gl.so");
    (void)asset_path("libglyphshift_vk.so");
    (void)asset_path("libglyphshift_editor.so");
    const auto manifest = asset_path("layers/VK_LAYER_GLYPHSHIFT_overlay.json");
    const auto directory =
        std::filesystem::path(manifest).parent_path().string();
    // VK_LAYER_PATH overrides all normal discovery, including
    // VK_ADD_LAYER_PATH. Preserve that explicit override when set; otherwise
    // add our development directory without hiding system validation layers or
    // existing additions.
    const char* path_variable =
        std::getenv("VK_LAYER_PATH") ? "VK_LAYER_PATH" : "VK_ADD_LAYER_PATH";
    std::string search_path = directory;
    if (const char* inherited = std::getenv(path_variable);
        inherited && *inherited)
      search_path += ":" + std::string(inherited);
    if (setenv(path_variable, search_path.c_str(), 1) != 0)
      throw system_error("set Vulkan layer discovery path");
    std::string layers = "VK_LAYER_GLYPHSHIFT_overlay";
    if (const char* inherited = std::getenv("VK_INSTANCE_LAYERS");
        inherited && *inherited)
      layers += ":" + std::string(inherited);
    if (setenv("VK_INSTANCE_LAYERS", layers.c_str(), 1) != 0)
      throw system_error("enable Vulkan layer");
    if (const char* inherited = std::getenv("LD_PRELOAD");
        inherited && *inherited)
      preload += ":" + std::string(inherited);
    if (setenv("LD_PRELOAD", preload.c_str(), 1) != 0)
      throw system_error("set LD_PRELOAD");
    // Keep the launched process and its descendants unable to gain privileges,
    // including if file metadata changes between inspection and execution.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
      throw system_error("set no_new_privs");
    // Execute the inspected inode instead of resolving the pathname again.
    // Replacing this wrapper preserves the target's exit status and signals.
    fexecve(file.get(), argv + target_arg, environ);
    if (errno == ENOENT) failure = 127;
    throw system_error("execute target");
  } catch (const std::exception& error) {
    std::cerr << "glyphshift: " << error.what() << '\n';
    return failure;
  }
}
