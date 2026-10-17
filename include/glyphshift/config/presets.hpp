#pragma once
#include <array>
#include <filesystem>
#include <glyphshift/config/settings.hpp>
#include <string>
#include <vector>
namespace glyphshift {
constexpr bool preset_name_valid(std::string_view name) noexcept {
  if (name.empty() || name.size() > 64 || name == "." || name == "..")
    return false;
  for (char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
      return false;
  return name.front() != '.';
}
// Stable path identity; executable updates keep their profile. Explicit IDs
// let a Proton invocation identify the game rather than the Wine executable.
inline std::array<char, 17> profile_id(std::string_view executable) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : executable) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::array<char, 17> out{};
  for (unsigned i = 0; i < 16; ++i)
    out[15 - i] = "0123456789abcdef"[(hash >> (i * 4)) & 15];
  return out;
}
class __attribute__((visibility("default"))) PresetStore {
 public:
  PresetStore();
  PresetStore(std::filesystem::path config_home, std::string profile);
  bool load(std::string_view name, Settings& settings,
            std::string& error) const;
  bool save(std::string_view name, const Settings& settings,
            std::string& error) const;
  bool select(std::string_view name, std::string& error) const;
  std::string selected(std::string& error) const;
  std::vector<std::string> names() const;
  const std::filesystem::path& directory() const { return directory_; }

 private:
  std::filesystem::path directory_;
};
__attribute__((visibility("default"))) std::string serialize_settings(
    const Settings& settings);
}  // namespace glyphshift
