#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace glyphshift {
inline constexpr std::array<std::string_view, 7>
    character_sets{"classic", "ascii", "alphanumeric", "binary", "blocks", "braille", "custom"};

struct CharacterSelection {
    unsigned set = 0;
    std::array<char, 1025> text{" .:-=+*#%@"};
    std::array<char, 1025> font{};
    std::array<char, 1025> edges{"|-/\\"};
    // Optional glyphs used only by local shape matching, never by tonal fill.
    std::array<char, 1025> shapes{};
    bool density_order = false;
    bool reverse = false;
    // Extra outline stroke for font glyphs, 0..1 (0 keeps the face's own weight).
    float weight = 0;
    bool operator==(const CharacterSelection&) const = default;
};

struct Codepoints {
    std::array<std::uint32_t, 256> values{};
    unsigned count = 0;
};

__attribute__((visibility("default"))) bool decode_characters(std::string_view text,
    Codepoints& output) noexcept;
__attribute__((visibility("default"))) bool valid_characters(
    const CharacterSelection& selection) noexcept;
inline constexpr unsigned character_tile_width = 16, character_tile_height = 24;
inline constexpr unsigned character_atlas_width = 16 * character_tile_width;
inline constexpr unsigned character_atlas_height = 17 * character_tile_height;
inline constexpr unsigned character_atlas_bytes = character_atlas_width * character_atlas_height;
inline constexpr unsigned integral_width = 16 * (character_tile_width + 1);
inline constexpr unsigned integral_height = 17 * (character_tile_height + 1);
inline constexpr unsigned integral_bytes = integral_width * integral_height * sizeof(float);
// Rows 0..3: tonal/structural lookups. Row 4, texel 0: matchable glyph count - 1.
// Rows 5..20: 4x4 region coverage descriptors (row 5 + region, column glyph).
inline constexpr unsigned ramp_height = 21;
inline constexpr unsigned descriptor_row = 5;

struct CharacterAtlas {
    std::array<std::uint8_t, character_atlas_bytes> pixels{};
    // Row zero preserves manual bins; row one maps tone to measured coverage.
    // Row two matches 2x2 light/dark structure to the actual selected masks.
    // Row three reserves partial blocks for structure rather than tonal texture.
    // Rows four onward describe every matchable glyph (fill, then shape-only).
    std::array<std::uint8_t, 256 * ramp_height> ramp{};
    // Per-glyph summed areas with zero borders: exact box filtering at any scale.
    std::array<float, integral_width * integral_height> integral{};
    unsigned count = 10;
    // Shape-only glyphs occupy slots count..count + shapes - 1.
    unsigned shapes = 0;
    std::array<char, 256> diagnostic{};
};

// Largest mean squared descriptor error accepted for a local shape glyph.
inline constexpr float shape_fit = .06F;

// Mirrors the analysis shader's shape matching for 4x4 region luminances
// (row-major). Returns the best-fitting matchable glyph, or -1 for a flat
// cell or when no glyph fits within shape_fit. `invert` matches the dark group.
__attribute__((visibility("default"))) int shape_glyph(const CharacterAtlas& atlas,
    std::span<const float, 16> levels,
    bool invert) noexcept;

// CPU-only, called at lazy renderer initialization or selection changes.
// Invalid/unavailable fonts or characters fall back to the built-in classic.
__attribute__((visibility("default"))) CharacterAtlas build_character_atlas(
    const CharacterSelection& selection);
}  // namespace glyphshift
