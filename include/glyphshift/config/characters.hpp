#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace glyphshift {
inline constexpr std::array<std::string_view, 7>
    character_sets{"classic", "ascii", "alphanumeric", "binary", "blocks", "braille", "custom"};

struct CharacterSelection {
    unsigned set = 0;
    std::array<char, 1025> text{" .:-=+*#%@"};
    std::array<char, 1025> font{};
    std::array<char, 1025> edges{"|-/\\"};
    bool density_order = false;
    bool reverse = false;
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

struct CharacterAtlas {
    std::array<std::uint8_t, character_atlas_bytes> pixels{};
    // Row zero preserves manual bins; row one maps tone to measured coverage.
    std::array<std::uint8_t, 512> ramp{};
    // Per-glyph summed areas with zero borders: exact box filtering at any scale.
    std::array<float, integral_width * integral_height> integral{};
    unsigned count = 10;
    std::array<char, 256> diagnostic{};
};

// CPU-only, called at lazy renderer initialization or selection changes.
// Invalid/unavailable fonts or characters fall back to the built-in classic.
__attribute__((visibility("default"))) CharacterAtlas build_character_atlas(
    const CharacterSelection& selection);
}  // namespace glyphshift
