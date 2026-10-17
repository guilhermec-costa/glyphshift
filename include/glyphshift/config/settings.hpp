#pragma once

#include <cstdint>
#include <glyphshift/config/characters.hpp>
#include <string_view>

namespace glyphshift {
struct ArtSettings {
    bool illustrated = false;
    bool fill = true, contour_overlay = true;
    float exposure = 1.0F, contrast = 1.0F, gamma = 1.0F;
    float edge_strength = 1.0F, edge_threshold = 0.18F;
    std::array<float, 3> foreground{1, 1, 1}, background{0, 0, 0};
    bool operator==(const ArtSettings&) const = default;
};

__attribute__((visibility("default"))) bool valid_art(const ArtSettings&) noexcept;

struct Settings {
    unsigned cell_width = 8;
    unsigned cell_height = 12;
    bool source_color = true;
    bool enabled = true;
    float glyph_strength = 0.45F;
    CharacterSelection glyphs{};
    ArtSettings art{};
    bool operator==(const Settings&) const = default;
};

// Four vec4s, shared by the GL uniforms and Vulkan push constants.
inline std::array<float, 16> art_parameters(const Settings& s) {
    const auto& a = s.art;
    return {a.exposure,
        a.contrast,
        a.gamma,
        a.contour_overlay ? 1.0F : 0.0F,
        a.illustrated ? 1.0F : 0.0F,
        a.edge_strength,
        a.edge_threshold,
        a.fill ? 1.0F : 0.0F,
        a.foreground[0],
        a.foreground[1],
        a.foreground[2],
        0,
        a.background[0],
        a.background[1],
        a.background[2],
        0};
}

enum SettingWarning : std::uint32_t {
    invalid_width = 1,
    invalid_height = 2,
    invalid_color = 4,
    invalid_enabled = 8,
    unknown_setting = 16,
    invalid_strength = 32,
    invalid_characters = 64,
    invalid_art = 128
};

struct ParsedSettings {
    Settings value;
    std::uint32_t warnings = 0;
};

__attribute__((visibility("default"))) ParsedSettings parse_settings(
    std::string_view text) noexcept;
// GLYPHSHIFT_CONFIG is optional. Stat is allocation-free on the steady path;
// files are read only after a change, never framebuffer data. No graphics API.
__attribute__((visibility("default"))) Settings runtime_settings() noexcept;
}  // namespace glyphshift
