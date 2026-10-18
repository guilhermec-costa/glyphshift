#pragma once

#include <cstddef>
#include <cstdint>
#include <glyphshift/config/characters.hpp>
#include <string_view>

namespace glyphshift {
struct ArtSettings {
    bool illustrated = false;
    bool fill = true, contour_overlay = true;
    bool shape_matching = false;
    // Interpolate cell paper between cell centers: a wash instead of tiles.
    bool paper_wash = false;
    // Binary glyph masks: every pixel is ink or paper, like pixel-art glyphs.
    bool crisp = false;
    // Contour detector: false votes on a 5x5 cell lattice; true uses a
    // pixel-level difference of Gaussians, voted per cell.
    bool dog_edges = false;
    // DoG blur sigma in pixels (0.3..4), band threshold (0..0.2) and the share
    // of a cell's samples that must agree on a direction (0..1).
    float dog_sigma = 1.0F, dog_threshold = 0.025F, edge_votes = 0.2F;
    float shape_threshold = 0.2F, scene_paper = 0.0F;
    float exposure = 1.0F, contrast = 1.0F, gamma = 1.0F;
    float edge_strength = 1.0F, edge_threshold = 0.18F;
    // Source-color saturation (0..2) and glow/ink bleed from neighboring glyphs (0..1).
    float saturation = 1.0F, glow = 0.0F;
    // Temporal hysteresis for illustrated glyph choices (0 off .. 1 strongest).
    float stability = 0.0F;
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

// Art parameters as vec4s, shared by the GL uniforms and a Vulkan uniform
// buffer (push constants stay at Vulkan 1.0's guaranteed 128 bytes without
// them). Renderers zero the stability entry whenever no valid previous frame
// exists. Entries after the last used one are zero.
inline constexpr std::size_t art_vectors = 8;
inline constexpr std::size_t art_stability = 19;
using ArtParameters = std::array<float, 4 * art_vectors>;

inline ArtParameters art_parameters(const Settings& s) {
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
        a.shape_matching ? a.shape_threshold : -1.0F,
        a.background[0],
        a.background[1],
        a.background[2],
        a.scene_paper,
        a.saturation,
        a.glow,
        // Bit flags: 1 paper wash, 2 crisp glyphs, 4 DoG contours.
        (a.paper_wash ? 1.0F : 0.0F) + (a.crisp ? 2.0F : 0.0F) + (a.dog_edges ? 4.0F : 0.0F),
        a.stability,
        a.dog_sigma,
        a.dog_threshold,
        a.edge_votes};
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
