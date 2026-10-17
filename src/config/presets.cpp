#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <glyphshift/config/presets.hpp>
#include <stdexcept>

namespace glyphshift {
namespace {
struct File {
  int fd;
  ~File() {
    if (fd >= 0) close(fd);
  }
};
std::filesystem::path config_home() {
  if (const char* value = std::getenv("XDG_CONFIG_HOME");
      value && *value && std::filesystem::path(value).is_absolute())
    return value;
  if (const char* value = std::getenv("HOME");
      value && *value && std::filesystem::path(value).is_absolute())
    return std::filesystem::path(value) / ".config";
  throw std::runtime_error("presets need absolute XDG_CONFIG_HOME or HOME");
}
std::string profile() {
  if (const char* value = std::getenv("GLYPHSHIFT_PROFILE")) {
    if (!preset_name_valid(value))
      throw std::runtime_error("invalid GLYPHSHIFT_PROFILE");
    return value;
  }
  return profile_id(std::filesystem::read_symlink("/proc/self/exe").string())
      .data();
}
int open_directory(const std::filesystem::path& path, bool create,
                   std::string& error) {
  if (create) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
      error = "cannot create preset directory: " + ec.message();
      return -1;
    }
  }
  const int fd =
      open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0 && (create || errno != ENOENT))
    error =
        "cannot open preset directory: " + std::string(std::strerror(errno));
  return fd;
}
bool read_file(int dir, const std::string& name, std::string& text,
               std::string& error) {
  const File file{openat(dir, name.c_str(),
                         O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
  if (file.fd < 0) {
    if (errno != ENOENT)
      error = "cannot read preset: " + std::string(std::strerror(errno));
    return false;
  }
  struct stat status{};
  if (fstat(file.fd, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size > 4096) {
    error = "preset must be a regular file of at most 4096 bytes";
    return false;
  }
  std::array<char, 4097> buffer{};
  std::size_t count = 0;
  while (count < buffer.size()) {
    const auto n = read(file.fd, buffer.data() + count, buffer.size() - count);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) {
      error = "cannot read preset: " + std::string(std::strerror(errno));
      return false;
    }
    if (!n) break;
    count += static_cast<std::size_t>(n);
  }
  if (count > 4096) {
    error = "preset too large";
    return false;
  }
  text.assign(buffer.data(), count);
  return true;
}
bool atomic_file(int dir, const std::string& name, std::string_view text,
                 std::string& error) {
  static std::atomic<unsigned> serial{0};
  const std::string temporary = ".tmp-" + std::to_string(getpid()) + "-" +
                                std::to_string(serial.fetch_add(1));
  const File file{openat(dir, temporary.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                         0600)};
  if (file.fd < 0) {
    error = "cannot create preset temporary file: " +
            std::string(std::strerror(errno));
    return false;
  }
  std::size_t count = 0;
  bool ok = true;
  while (count < text.size()) {
    const auto n = write(file.fd, text.data() + count, text.size() - count);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      ok = false;
      break;
    }
    count += static_cast<std::size_t>(n);
  }
  if (ok) ok = fsync(file.fd) == 0;
  if (ok) ok = renameat(dir, temporary.c_str(), dir, name.c_str()) == 0;
  if (ok) ok = fsync(dir) == 0;
  if (!ok) {
    error = "cannot commit preset: " + std::string(std::strerror(errno));
    unlinkat(dir, temporary.c_str(), 0);
  }
  return ok;
}
}  // namespace
PresetStore::PresetStore() : PresetStore(config_home(), profile()) {}
PresetStore::PresetStore(std::filesystem::path home, std::string id) {
  if (!home.is_absolute() || !preset_name_valid(id))
    throw std::invalid_argument("invalid preset directory/profile");
  directory_ = std::move(home) / "glyphshift" / "profiles" / id;
}
std::string serialize_settings(const Settings& s) {
  // Locale-independent shortest round-trip; games may change LC_NUMERIC.
  std::array<char, 64> strength{};
  const auto [end, error] = std::to_chars(
      strength.data(), strength.data() + strength.size(), s.glyph_strength);
  if (error != std::errc{})
    throw std::runtime_error("cannot serialize glyph strength");
  return "enabled=" + std::string(s.enabled ? "true" : "false") +
         "\ncell_width=" + std::to_string(s.cell_width) +
         "\ncell_height=" + std::to_string(s.cell_height) +
         "\ncolor_mode=" + (s.source_color ? "source" : "mono") +
         "\nglyph_strength=" + std::string(strength.data(), end) + "\n";
}
bool PresetStore::load(std::string_view name, Settings& settings,
                       std::string& error) const {
  error.clear();
  settings = {};
  if (!preset_name_valid(name)) {
    error =
        "invalid preset name (letters, digits, dot, underscore or hyphen; max "
        "64)";
    return false;
  }
  const File dir{open_directory(directory_, false, error)};
  if (dir.fd < 0) return false;
  std::string text;
  if (!read_file(dir.fd, std::string(name) + ".conf", text, error))
    return false;
  const auto parsed = parse_settings(text);
  if (parsed.warnings) {
    error = "invalid preset contents; using defaults";
    return false;
  }
  settings = parsed.value;
  return true;
}
bool PresetStore::save(std::string_view name, const Settings& settings,
                       std::string& error) const {
  error.clear();
  if (!preset_name_valid(name)) {
    error =
        "invalid preset name (letters, digits, dot, underscore or hyphen; max "
        "64)";
    return false;
  }
  if (!std::isfinite(settings.glyph_strength) || settings.glyph_strength < 0 ||
      settings.glyph_strength > 1) {
    error = "invalid glyph strength";
    return false;
  }
  if (settings.cell_width < 2 || settings.cell_width > 128 ||
      settings.cell_height < 2 || settings.cell_height > 128) {
    error = "invalid preset cell dimensions";
    return false;
  }
  const File dir{open_directory(directory_, true, error)};
  if (dir.fd < 0) return false;
  return atomic_file(dir.fd, std::string(name) + ".conf",
                     serialize_settings(settings), error);
}
bool PresetStore::select(std::string_view name, std::string& error) const {
  error.clear();
  if (!preset_name_valid(name)) {
    error = "invalid preset name";
    return false;
  }
  const File dir{open_directory(directory_, true, error)};
  if (dir.fd < 0) return false;
  return atomic_file(dir.fd, "active", std::string(name) + "\n", error);
}
std::string PresetStore::selected(std::string& error) const {
  error.clear();
  const File dir{open_directory(directory_, false, error)};
  if (dir.fd < 0) return "default";
  std::string text;
  if (!read_file(dir.fd, "active", text, error)) return "default";
  if (!text.empty() && text.back() == '\n') text.pop_back();
  if (!preset_name_valid(text)) {
    error = "invalid active preset; using default";
    return "default";
  }
  return text;
}
std::vector<std::string> PresetStore::names() const {
  std::vector<std::string> result{"default"};
  std::error_code ec;
  for (std::filesystem::directory_iterator it(directory_, ec), end;
       !ec && it != end; it.increment(ec)) {
    const auto name = it->path().stem().string();
    if (it->path().extension() == ".conf" && preset_name_valid(name) &&
        name != "default")
      result.push_back(name);
  }
  std::sort(result.begin(), result.end());
  return result;
}
}  // namespace glyphshift
