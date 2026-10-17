#include <algorithm>
#include <cmath>
#include <cstring>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/core/glyph.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
float prefix(const glyphshift::CharacterAtlas& a, unsigned g, float x,
             float y) {
  const unsigned ix = static_cast<unsigned>(x), iy = static_cast<unsigned>(y);
  const unsigned jx = std::min(ix + 1, 16U), jy = std::min(iy + 1, 24U);
  const auto at = [&](unsigned px, unsigned py) {
    return a.integral[((g / 16) * 25 + py) * glyphshift::integral_width +
                      (g % 16) * 17 + px];
  };
  const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
  return (at(ix, iy) * (1 - fx) + at(jx, iy) * fx) * (1 - fy) +
         (at(ix, jy) * (1 - fx) + at(jx, jy) * fx) * fy;
}
}  // namespace
int main() {
  using namespace glyphshift;
  try {
    check(contour_glyph(std::array<float, 9>{0, 0, 1, 0, 0, 1, 0, 0, 1}, 1,
                        .18F) == 0,
          "vertical contour");
    check(contour_glyph(std::array<float, 9>{0, 0, 0, 0, 0, 0, 1, 1, 1}, 1,
                        .18F) == 1,
          "horizontal contour");
    check(contour_glyph(std::array<float, 9>{0, 0, 0, 0, .5F, 1, 0, 1, 1}, 1,
                        .18F) == 2,
          "slash contour");
    check(contour_glyph(std::array<float, 9>{0, 1, 1, 0, .5F, 1, 0, 0, 0}, 1,
                        .18F) == 3,
          "backslash contour");
    check(contour_glyph(std::array<float, 9>{1, 0, 1, 0, 1, 0, 1, 0, 1}, 1,
                        .01F) == -1,
          "symmetric high-frequency texture is not a directional contour");
    check(contour_glyph(std::array<float, 9>{0, 0, 1, 0, 0, 1, 0, 0, 1}, 0,
                        0) == -1,
          "zero strength disables contours");
    check(contour_glyph(std::array<float, 9>{}, 1, 0) == -1,
          "flat cells have no contour");
    check(tone_luminance(0, 1, 1, 1) == 0 && tone_luminance(1, 1, 1, 1) == 1,
          "tone endpoints");
    check(std::abs(tone_luminance(.25F, 1, 1, 2) - .5F) < .000001F,
          "gamma response");
    const auto parsed = parse_settings(
        "render_style=illustrated\nfill_enabled=false\ncontour_overlay="
        "false\ntone_exposure=1.4\ntone_"
        "contrast=1.2\ntone_gamma=0.8\nedge_strength=0.7\nedge_threshold=0."
        "2\nforeground=0.2,0.3,0.4\nbackground=1,0.9,0.8\n");
    check(!parsed.warnings && parsed.value.art.illustrated &&
              !parsed.value.art.fill && !parsed.value.art.contour_overlay,
          "illustration config");
    check(
        parse_settings(serialize_settings(parsed.value)).value == parsed.value,
        "all art controls persist exactly");
    for (const char* text :
         {"tone_exposure=nan", "tone_contrast=0", "tone_gamma=inf",
          "edge_threshold=-1", "edge_strength=1.1", "render_style=bad",
          "fill_enabled=bad", "contour_overlay=bad", "foreground=1,2,0",
          "background=0,0", "background=0,0,0,0"}) {
      const auto bad = parse_settings(text);
      check((bad.warnings & invalid_art) && bad.value.art == ArtSettings{},
            "invalid art input has bounded defaults");
    }
    auto invalid = ArtSettings{};
    invalid.gamma = std::numeric_limits<float>::quiet_NaN();
    check(!valid_art(invalid), "nonfinite settings cannot be saved");
    check(
        parse_settings("edge_characters=\"|\"\n").warnings & invalid_characters,
        "four contour glyphs required");
    auto selection = CharacterSelection{};
    selection.set = 5;
    selection.density_order = true;
    const auto atlas = build_character_atlas(selection);
    check(!atlas.diagnostic[0] && atlas.count == 256,
          "full fill set plus independent contour tiles");
    for (unsigned g = 0; g < 260; ++g)
      for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 16; ++x) {
          const float xx = static_cast<float>(x), yy = static_cast<float>(y);
          const float reconstructed =
              prefix(atlas, g, xx + 1, yy + 1) - prefix(atlas, g, xx, yy + 1) -
              prefix(atlas, g, xx + 1, yy) + prefix(atlas, g, xx, yy);
          const auto expected =
              atlas.pixels[((g / 16) * 24 + y) * character_atlas_width +
                           (g % 16) * 16 + x];
          check(reconstructed == expected,
                "summed areas reconstruct every fill/edge texel without "
                "neighboring tile bleed");
        }
    // The same glyph's total coverage must survive every tested output size,
    // including non-square minification and strong magnification.
    for (const auto size : {std::array<unsigned, 2>{2, 2},
                            {4, 4},
                            {8, 12},
                            {13, 19},
                            {64, 96},
                            {128, 128}}) {
      for (unsigned g : {1U, 127U, 255U, 256U, 258U}) {
        float sum = 0;
        for (unsigned y = 0; y < size[1]; ++y)
          for (unsigned x = 0; x < size[0]; ++x) {
            const float x0 =
                static_cast<float>(x) * 16 / static_cast<float>(size[0]);
            const float y0 =
                static_cast<float>(y) * 24 / static_cast<float>(size[1]);
            const float x1 =
                static_cast<float>(x + 1) * 16 / static_cast<float>(size[0]);
            const float y1 =
                static_cast<float>(y + 1) * 24 / static_cast<float>(size[1]);
            sum += prefix(atlas, g, x1, y1) - prefix(atlas, g, x0, y1) -
                   prefix(atlas, g, x1, y0) + prefix(atlas, g, x0, y0);
          }
        check(std::abs(sum - prefix(atlas, g, 16, 24)) < 2,
              "area filtering conserves ink at all cell sizes");
      }
    }
    float previous = -1;
    for (unsigned bin = 0; bin < 256; ++bin) {
      const float coverage = prefix(atlas, atlas.ramp[256 + bin], 16, 24);
      check(coverage >= previous, "density-calibrated tone ramp is monotonic");
      previous = coverage;
    }
    std::cout << "Contours, tone, persistence, exact atlas areas and scale "
                 "conservation passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
