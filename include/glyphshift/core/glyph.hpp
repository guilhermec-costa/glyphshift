#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace glyphshift {
inline constexpr std::string_view default_characters = " .:-=+*#%@";
inline constexpr unsigned glyph_size = 8;
inline constexpr unsigned atlas_width =
    glyph_size * static_cast<unsigned>(default_characters.size());
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
// Automatic exposure, mirrored by the shader. The meter reports the scene's
// geometric-mean luminance; the exposure factor moves it toward exposure_key
// with the given strength (0..1), limited to 0.5..4.
inline constexpr float exposure_key = .4F, exposure_floor = 1.0F / 255;
inline constexpr float exposure_adaptation_seconds = .35F;
float auto_exposure_factor(float mean, float strength) noexcept;
// Share of the way from the adapted to the measured luminance after
// `seconds` (exponential; 1 when there is no previous value).
float exposure_adaptation(float seconds) noexcept;
// Adapts in the logarithmic domain, as brightness is perceived.
float adapt_luminance(float previous, float measured, float rate) noexcept;
// Local contrast, mirrored by the analysis shader, over 7x7 luminance samples
// of a cell's neighborhood (local_contrast_span of the image height around
// it), in the unclipped tone ((luminance * exposure - 0.5) * contrast + 0.5,
// before the clamp and gamma). A base/detail split: the samples'
// edge-preserving mean (local_base) is the cell's base; the base is rolled off
// into 0..1 (local_rolloff), and the cell's detail around it is amplified
// where the neighborhood is compressed (local_detail_gain). The result, after
// gamma, is blended with the global tone by strength (0..1). The contour and
// shape thresholds are divided by local_contrast_gain of the luminance
// deviation, at least 1 and at most local_contrast_max_gain, scaled toward 1
// by strength.
inline constexpr float local_contrast_target = .12F, local_contrast_max_gain = 4.0F;
inline constexpr float local_contrast_span = 1.0F / 12;
float local_contrast_gain(float deviation, float strength) noexcept;
// Bilateral mean and spread: a sample counts by exp(-d^2 / 2), d its distance
// from the cell's value over local_contrast_range, so a dark object beside a
// bright region neither darkens nor brightens the region around it (no halo).
inline constexpr float local_contrast_range = .25F;

struct LocalBase {
    float base, spread;
};

LocalBase local_base(float value, std::span<const float> samples) noexcept;
// Identity between 1 - local_contrast_knee and local_contrast_knee; beyond, an
// exponential roll-off that approaches 0 and 1 without reaching them, so
// clipped regions come back inside the ramp in their original order.
inline constexpr float local_contrast_knee = .7F;
float local_rolloff(float base) noexcept;
// local_detail_target / spread, between 1 and local_contrast_max_gain, and
// eased to 1 on flat areas (spread below local_contrast_flat_low..high). The
// target is wide enough that a region's detail spans about two glyph classes
// of a short ramp.
inline constexpr float local_detail_target = .25F;
inline constexpr float local_contrast_flat_low = .004F, local_contrast_flat_high = .012F;
float local_detail_gain(float spread) noexcept;
// Highlight recovery: a base clipped beyond the top (fully from
// local_contrast_clipped, easing in from 1) is lowered, after the roll-off, to
// at most 1 - local_contrast_headroom * spread * detail gain, so a region the
// exposure blew out shows its detail inside the ramp. Bases within the ramp
// (a bright sky) are left alone.
inline constexpr float local_contrast_headroom = 2.0F, local_contrast_clipped = 1.15F;
float local_recovery(float base, float spread) noexcept;
// The cell's unclipped value with base and detail adjusted:
// local_recovery(base, spread) + (value - base) * local_detail_gain(spread).
float local_contrast(float value, std::span<const float> samples) noexcept;
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
