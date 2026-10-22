#pragma once

#include <cstddef>
#include <cstdint>
#include <glyphshift/config/api.hpp>
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
    // Exposure follows the scene's metered brightness (0 off .. 1 fully).
    float auto_exposure = 0.0F;
    // Tone relative to each cell's neighborhood (0 off .. 1 fully), so fogged
    // distant regions keep their internal structure (glyphshift::local_rank,
    // local_equalization). On by default since 2026-10-22.
    float local_contrast = 0.5F;
    // Illustrated cells follow the camera's motion, estimated on the GPU, so
    // scrolling scenes keep their glyphs instead of crawling across the grid.
    bool anchor_grid = false;
    std::array<float, 3> foreground{1, 1, 1}, background{0, 0, 0};
    bool operator==(const ArtSettings&) const = default;
};

GS_EDITOR_API bool valid_art(const ArtSettings&) noexcept;

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
inline constexpr std::size_t art_stability = 19, art_local_contrast = 23;
// Renderers set the metering's adaptation rate for the frame (1: start fresh)
// and whether the anchored grid's camera state continues the previous frame.
inline constexpr std::size_t art_auto_exposure = 24, art_adaptation = 25;
inline constexpr std::size_t art_anchor = 26, art_anchor_history = 27;
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
        a.edge_votes,
        a.local_contrast,
        a.auto_exposure,
        1,
        a.anchor_grid ? 1.0F : 0.0F,
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

GS_EDITOR_API ParsedSettings parse_settings(std::string_view text) noexcept;
// GLYPHSHIFT_CONFIG is optional. Stat is allocation-free on the steady path;
// files are read only after a change, never framebuffer data. No graphics API.
GS_EDITOR_API Settings runtime_settings() noexcept;
}  // namespace glyphshift
