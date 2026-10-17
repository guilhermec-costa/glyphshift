#pragma once

#include <cstdint>
#include <glyphshift/config/characters.hpp>
#include <string_view>

namespace glyphshift {
struct Settings {
  unsigned cell_width = 8;
  unsigned cell_height = 12;
  bool source_color = true;
  bool enabled = true;
  float glyph_strength = 0.45F;
  CharacterSelection glyphs{};
  bool operator==(const Settings&) const = default;
};
enum SettingWarning : std::uint32_t {
  invalid_width = 1,
  invalid_height = 2,
  invalid_color = 4,
  invalid_enabled = 8,
  unknown_setting = 16,
  invalid_strength = 32,
  invalid_characters = 64
};
struct ParsedSettings {
  Settings value;
  std::uint32_t warnings = 0;
};
__attribute__((visibility("default"))) ParsedSettings
parse_settings(std::string_view text) noexcept;
// GLYPHSHIFT_CONFIG is optional. Stat is allocation-free on the steady path;
// files are read only after a change, never framebuffer data. No graphics API.
__attribute__((visibility("default"))) Settings runtime_settings() noexcept;
}  // namespace glyphshift
