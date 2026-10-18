#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace glyphshift {
inline constexpr std::string_view default_characters = " .:-=+*#%@";
inline constexpr unsigned glyph_size = 8;
inline constexpr unsigned atlas_width = glyph_size * default_characters.size();
inline constexpr unsigned atlas_height = glyph_size;
inline constexpr std::array<std::uint8_t, 10> default_ramp{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
// Original 8x8 bitmap font for the M3 ramp, row zero at the top. This is data,
// shared by both renderers; the shader receives the ramp as a lookup texture.
inline constexpr std::array<std::array<std::uint8_t, 8>, 10> glyph_rows{{{0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0x18, 0x18, 0},
    {0, 0x18, 0x18, 0, 0x18, 0x18, 0, 0},
    {0, 0, 0, 0x7e, 0x7e, 0, 0, 0},
    {0, 0, 0x3c, 0x3c, 0, 0x3c, 0x3c, 0},
    {0, 0x18, 0x18, 0x7e, 0x7e, 0x18, 0x18, 0},
    {0, 0x66, 0x3c, 0x7e, 0x7e, 0x3c, 0x66, 0},
    {0, 0x66, 0xff, 0x66, 0x66, 0xff, 0x66, 0},
    {0xe3, 0xe6, 0xec, 0x18, 0x30, 0x67, 0xc7, 0x87},
    {0x7e, 0xff, 0xc3, 0xdf, 0xdb, 0xdf, 0xc0, 0x7e}}};
inline constexpr auto default_atlas = [] {
    std::array<std::uint8_t, atlas_width * atlas_height> pixels{};
    for (unsigned glyph = 0; glyph < glyph_rows.size(); ++glyph)
        for (unsigned y = 0; y < glyph_size; ++y)
            for (unsigned x = 0; x < glyph_size; ++x)
                pixels[y * atlas_width + glyph * glyph_size + x] =
                    (glyph_rows[glyph][y] & (1U << (7 - x))) ? 255 : 0;
    return pixels;
}();
float tone_luminance(float value, float exposure, float contrast, float gamma) noexcept;
// Row-major 3x3 luminance neighborhood. -1 means fill, 0..3 are | - / \\.
int contour_glyph(std::span<const float, 9> levels, float strength, float threshold) noexcept;
// Row-major 5x5 luminance lattice at quarter-cell steps, spanning the cell and
// the first pixels of its right/lower neighbors. Each interior point applies
// contour_glyph (differences doubled to half-cell units); an axis wins with at
// least three votes and twice any other axis. Mirrors the analysis shader.
int contour_vote(std::span<const float, 25> lattice, float strength, float threshold) noexcept;
float luminance(float red, float green, float blue) noexcept;
std::uint8_t mapped_glyph(float luminance, std::span<const std::uint8_t> ramp) noexcept;
}  // namespace glyphshift
