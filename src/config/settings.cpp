#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <glyphshift/config/settings.hpp>
#include <mutex>
#include <string>

namespace glyphshift {
namespace {
std::string_view trim(std::string_view text) {
  constexpr auto whitespace = " \t\r";
  const auto start = text.find_first_not_of(whitespace);
  if (start == std::string_view::npos) return {};
  return text.substr(start, text.find_last_not_of(whitespace) - start + 1);
}
bool cell(std::string_view text, unsigned& output) {
  unsigned value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value < 2 ||
      value > 128)
    return false;
  output = value;
  return true;
}
void warning(const char* message, std::size_t size) noexcept {
  const auto ignored = write(STDERR_FILENO, message, size);
  (void)ignored;
}
template <std::size_t N>
void warning(const char (&message)[N]) noexcept {
  warning(message, N - 1);
}
struct Stamp {
  dev_t device{};
  ino_t inode{};
  off_t size{};
  timespec modified{};
  int error = -1;
  bool operator==(const Stamp& other) const {
    return device == other.device && inode == other.inode &&
           size == other.size && modified.tv_sec == other.modified.tv_sec &&
           modified.tv_nsec == other.modified.tv_nsec && error == other.error;
  }
};
class Runtime {
 public:
  Runtime() {
    if (const char* path = std::getenv("GLYPHSHIFT_CONFIG")) path_ = path;
  }
  Settings get() {
    if (path_.empty()) return {};
    const std::lock_guard lock(mutex_);
    struct stat status{};
    Stamp stamp;
    if (stat(path_.c_str(), &status) != 0)
      stamp.error = errno;
    else
      stamp = {status.st_dev, status.st_ino, status.st_size, status.st_mtim, 0};
    if (stamp == previous_) return current_;
    previous_ = stamp;
    current_ = {};
    if (stamp.error || !S_ISREG(status.st_mode) || status.st_size > 4096) {
      warning(
          "glyphshift: config must be a readable regular file of at most 4096 "
          "bytes; using defaults\n");
      return current_;
    }
    const int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
      warning("glyphshift: cannot read config; using defaults\n");
      return current_;
    }
    char buffer[4097];
    struct stat opened{};
    const auto count = fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode)
                           ? read(fd, buffer, sizeof(buffer))
                           : -1;
    close(fd);
    if (count < 0 || count > 4096) {
      warning(
          "glyphshift: cannot read config or config too large; using "
          "defaults\n");
      return current_;
    }
    const auto parsed =
        parse_settings({buffer, static_cast<std::size_t>(count)});
    current_ = parsed.value;
    if (parsed.warnings & invalid_width)
      warning("glyphshift: invalid cell_width; using 8 (range 2..128)\n");
    if (parsed.warnings & invalid_height)
      warning("glyphshift: invalid cell_height; using 12 (range 2..128)\n");
    if (parsed.warnings & invalid_color)
      warning("glyphshift: invalid color_mode; using source (source/mono)\n");
    if (parsed.warnings & invalid_strength)
      warning("glyphshift: invalid glyph_strength; using 0.45 (range 0..1)\n");
    if (parsed.warnings & invalid_enabled)
      warning("glyphshift: invalid enabled; using true (true/false/1/0)\n");
    if (parsed.warnings & unknown_setting)
      warning("glyphshift: unknown/malformed setting ignored\n");
    return current_;
  }

 private:
  std::string path_;
  std::mutex mutex_;
  Stamp previous_;
  Settings current_;
};
}  // namespace
ParsedSettings parse_settings(std::string_view text) noexcept {
  ParsedSettings parsed;
  const Settings defaults;
  while (!text.empty()) {
    const auto end = text.find('\n');
    auto line = trim(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view{}
                                         : text.substr(end + 1);
    line = trim(line.substr(0, line.find('#')));
    if (line.empty()) continue;
    const auto equal = line.find('=');
    if (equal == std::string_view::npos) {
      parsed.warnings |= unknown_setting;
      continue;
    }
    const auto key = trim(line.substr(0, equal));
    const auto value = trim(line.substr(equal + 1));
    if (key == "cell_width") {
      parsed.value.cell_width = defaults.cell_width;
      if (!cell(value, parsed.value.cell_width))
        parsed.warnings |= invalid_width;
    } else if (key == "cell_height") {
      parsed.value.cell_height = defaults.cell_height;
      if (!cell(value, parsed.value.cell_height))
        parsed.warnings |= invalid_height;
    } else if (key == "color_mode") {
      parsed.value.source_color = defaults.source_color;
      if (value == "mono")
        parsed.value.source_color = false;
      else if (value != "source")
        parsed.warnings |= invalid_color;
    } else if (key == "glyph_strength") {
      float strength = defaults.glyph_strength;
      const auto [parsed_end, error] =
          std::from_chars(value.data(), value.data() + value.size(), strength);
      parsed.value.glyph_strength = defaults.glyph_strength;
      if (error != std::errc{} || parsed_end != value.data() + value.size() ||
          !std::isfinite(strength) || strength < 0 || strength > 1)
        parsed.warnings |= invalid_strength;
      else
        parsed.value.glyph_strength = strength;
    } else if (key == "enabled") {
      parsed.value.enabled = defaults.enabled;
      if (value == "false" || value == "0")
        parsed.value.enabled = false;
      else if (value != "true" && value != "1")
        parsed.warnings |= invalid_enabled;
    } else
      parsed.warnings |= unknown_setting;
  }
  return parsed;
}
Settings runtime_settings() noexcept {
  try {
    static Runtime runtime;
    return runtime.get();
  } catch (...) {
    static const bool reported = [] {
      warning("glyphshift: settings unavailable; using defaults\n");
      return true;
    }();
    (void)reported;
    return {};
  }
}
}  // namespace glyphshift
