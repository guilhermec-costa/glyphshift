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
    if (!value)
        throw std::runtime_error(message);
}

float prefix(const glyphshift::CharacterAtlas& a, unsigned g, float x, float y) {
    const unsigned ix = static_cast<unsigned>(x), iy = static_cast<unsigned>(y);
    const unsigned jx = std::min(ix + 1, 16U), jy = std::min(iy + 1, 24U);
    const auto at = [&](unsigned px, unsigned py) {
        return a.integral[((g / 16) * 25 + py) * glyphshift::integral_width + (g % 16) * 17 + px];
    };
    const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
    return (at(ix, iy) * (1 - fx) + at(jx, iy) * fx) * (1 - fy) +
           (at(ix, jy) * (1 - fx) + at(jx, jy) * fx) * fy;
}
}  // namespace

int main() {
    using namespace glyphshift;
    try {
        check(contour_glyph(std::array<float, 9>{0, 0, 1, 0, 0, 1, 0, 0, 1}, 1, .18F) == 0,
            "vertical contour");
        check(contour_glyph(std::array<float, 9>{0, 0, 0, 0, 0, 0, 1, 1, 1}, 1, .18F) == 1,
            "horizontal contour");
        check(contour_glyph(std::array<float, 9>{0, 0, 0, 0, .5F, 1, 0, 1, 1}, 1, .18F) == 2,
            "slash contour");
        check(contour_glyph(std::array<float, 9>{0, 1, 1, 0, .5F, 1, 0, 0, 0}, 1, .18F) == 3,
            "backslash contour");
        check(contour_glyph(std::array<float, 9>{1, 0, 1, 0, 1, 0, 1, 0, 1}, 1, .01F) == -1,
            "symmetric high-frequency texture is not a directional contour");
        check(contour_glyph(std::array<float, 9>{0, 0, 1, 0, 0, 1, 0, 0, 1}, 0, 0) == -1,
            "zero strength disables contours");
        check(contour_glyph(std::array<float, 9>{}, 1, 0) == -1, "flat cells have no contour");
        check(tone_luminance(0, 1, 1, 1) == 0 && tone_luminance(1, 1, 1, 1) == 1, "tone endpoints");
        check(std::abs(tone_luminance(.25F, 1, 1, 2) - .5F) < .000001F, "gamma response");
        const auto parsed = parse_settings(
            "render_style=illustrated\nfill_enabled=false\ncontour_overlay="
            "false\ntone_exposure=1.4\ntone_"
            "contrast=1.2\ntone_gamma=0.8\nedge_strength=0.7\nedge_threshold=0."
            "2\nforeground=0.2,0.3,0.4\nbackground=1,0.9,0.8\n"
            "shape_matching=true\nshape_threshold=0.17\nscene_paper=0.65\n"
            "saturation=1.6\nglow=0.3\npaper_wash=true\nstability=0.7\n");
        check(!parsed.warnings && parsed.value.art.illustrated && !parsed.value.art.fill &&
                  !parsed.value.art.contour_overlay && parsed.value.art.saturation == 1.6F &&
                  parsed.value.art.glow == .3F && parsed.value.art.paper_wash &&
                  parsed.value.art.stability == .7F,
            "illustration config");
        check(art_parameters(parsed.value)[16] == 1.6F && art_parameters(parsed.value)[17] == .3F &&
                  art_parameters(parsed.value)[18] == 1 &&
                  art_parameters(parsed.value)[art_stability] == .7F,
            "saturation, glow and paper wash reach the shared shader parameters");
        check(parse_settings(serialize_settings(parsed.value)).value == parsed.value,
            "all art controls persist exactly");
        for (const char* text : {"tone_exposure=nan",
                 "tone_contrast=0",
                 "tone_gamma=inf",
                 "edge_threshold=-1",
                 "edge_strength=1.1",
                 "render_style=bad",
                 "fill_enabled=bad",
                 "contour_overlay=bad",
                 "shape_matching=bad",
                 "shape_threshold=nan",
                 "shape_threshold=-0.1",
                 "scene_paper=1.1",
                 "saturation=2.5",
                 "saturation=nan",
                 "glow=1.5",
                 "glow=-0.1",
                 "paper_wash=bad",
                 "stability=1.5",
                 "stability=-1",
                 "foreground=1,2,0",
                 "background=0,0",
                 "background=0,0,0,0"}) {
            const auto bad = parse_settings(text);
            check((bad.warnings & invalid_art) && bad.value.art == ArtSettings{},
                "invalid art input has bounded defaults");
        }
        auto invalid = ArtSettings{};
        invalid.gamma = std::numeric_limits<float>::quiet_NaN();
        check(!valid_art(invalid), "nonfinite settings cannot be saved");
        check(parse_settings("edge_characters=\"|\"\n").warnings & invalid_characters,
            "four contour glyphs required");
        const auto shape_selection =
            parse_settings("character_set=custom\ncharacters=\" █▘▝▀▖▌▞▛▗▚▐▜▄▙▟\"\n").value.glyphs;
        const auto shapes = build_character_atlas(shape_selection);
        for (unsigned pattern = 0; pattern < 16; ++pattern) {
            const auto glyph = shapes.ramp[512 + pattern];
            for (unsigned y = 0; y < 24; ++y)
                for (unsigned x = 0; x < 16; ++x) {
                    const auto q = (y >= 12 ? 2U : 0U) + (x >= 8 ? 1U : 0U);
                    const auto actual =
                        shapes.pixels[((glyph / 16) * 24 + y) * character_atlas_width +
                                      (glyph % 16) * 16 + x];
                    check(actual == ((pattern & (1U << q)) ? 255 : 0),
                        "all sixteen local silhouettes select exact quadrant masks");
                }
        }
        for (unsigned bin = 0; bin < 256; ++bin)
            check(shapes.ramp[768 + bin] < 2,
                "partial blocks are reserved for structure, not repeated tonal texture");
        const auto only_block = build_character_atlas(
            parse_settings("character_set=custom\ncharacters=\"▌\"\n").value.glyphs);
        check(only_block.ramp[768] == 0 && only_block.ramp[1023] == 0,
            "partial-block-only sets retain a valid tonal fallback");
        // Contour voting on the 5x5 analysis lattice.
        const auto lattice = [](auto lit) {
            std::array<float, 25> values{};
            for (unsigned j = 0; j < 5; ++j)
                for (unsigned i = 0; i < 5; ++i)
                    values[j * 5 + i] = lit(i, j) ? 1.0F : 0.0F;
            return values;
        };
        check(contour_vote(lattice([](unsigned i, unsigned) {
            return i == 4;
        }),
                  1,
                  .18F) == 0,
            "a boundary on the right cell edge belongs to this cell as a vertical contour");
        check(contour_vote(lattice([](unsigned i, unsigned) {
            return i >= 1;
        }),
                  1,
                  .18F) == 0 &&
                  contour_vote(lattice([](unsigned, unsigned j) {
                      return j >= 2;
                  }),
                      1,
                      .18F) == 1,
            "vertical and horizontal contours inside the cell");
        check(contour_vote(lattice([](unsigned i, unsigned j) {
            return i > j;
        }),
                  1,
                  .18F) == 3,
            "top-left to bottom-right diagonal is a backslash");
        check(contour_vote(lattice([](unsigned i, unsigned j) {
            return (i + j) % 2;
        }),
                  1,
                  .01F) == -1 &&
                  contour_vote(lattice([](unsigned, unsigned) {
                      return false;
                  }),
                      1,
                      0) == -1 &&
                  contour_vote(lattice([](unsigned i, unsigned) {
                      return i == 4;
                  }),
                      0,
                      0) == -1,
            "checkerboard texture, flat cells and zero strength have no contour");
        // Descriptor matching recovers exact quadrant silhouettes.
        for (unsigned pattern = 1; pattern < 15; ++pattern) {
            std::array<float, 16> levels{};
            for (unsigned r = 0; r < 16; ++r)
                levels[r] = (pattern & (1U << ((r / 8) * 2 + (r % 4) / 2))) ? .9F : .1F;
            const int glyph = shape_glyph(shapes, levels, false);
            check(glyph == shapes.ramp[512 + pattern],
                "descriptor matching selects the exact quadrant block");
            check(shape_glyph(shapes, levels, true) == shapes.ramp[512 + (15 - pattern)],
                "dark ink matches the inverted silhouette");
        }
        // Shape-only characters: matchable, never part of the tonal ramp.
        auto outline = parse_settings(
            "character_set=custom\ncharacters=\" .:-=+*#\"\nshape_characters=\"|_/\\\\\"\n");
        check(!outline.warnings && std::string_view(outline.value.glyphs.shapes.data()) == "|_/\\",
            "shape characters parse");
        check(parse_settings(serialize_settings(outline.value)).value == outline.value,
            "shape characters persist");
        const auto outlines = build_character_atlas(outline.value.glyphs);
        check(!outlines.diagnostic[0] && outlines.count == 8 && outlines.shapes == 4 &&
                  outlines.ramp[256 * 4] == 11,
            "shape glyphs occupy the slots after the fill set");
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned bin = 0; bin < 256; ++bin)
                check(row == 2 || outlines.ramp[256 * row + bin] < 8,
                    "tonal lookups never use shape-only glyphs");
        std::array<float, 16> bar{}, floor{}, checker{};
        for (unsigned r = 0; r < 16; ++r) {
            bar[r] = (r % 4 == 1 || r % 4 == 2) ? 1.0F : 0.0F;
            floor[r] = r >= 12 ? 1.0F : 0.0F;
            checker[r] = (r + r / 4) % 2 ? 1.0F : 0.0F;
        }
        check(shape_glyph(outlines, bar, false) == 8, "a centered bright column matches |");
        check(shape_glyph(outlines, floor, false) == 9, "a bright bottom row matches _");
        check(shape_glyph(outlines, checker, false) == -1,
            "texture that no glyph resembles keeps tonal fill");
        auto crowded = CharacterSelection{};
        crowded.set = 5;
        std::strcpy(crowded.shapes.data(), "|");
        check(!valid_characters(crowded), "fill and shape glyphs share 256 atlas slots");
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
                    const auto expected = atlas.pixels[((g / 16) * 24 + y) * character_atlas_width +
                                                       (g % 16) * 16 + x];
                    check(reconstructed == expected,
                        "summed areas reconstruct every fill/edge texel without "
                        "neighboring tile bleed");
                }
        // The same glyph's total coverage must survive every tested output size,
        // including non-square minification and strong magnification.
        for (const auto size :
            {std::array<unsigned, 2>{2, 2}, {4, 4}, {8, 12}, {13, 19}, {64, 96}, {128, 128}}) {
            for (unsigned g : {1U, 127U, 255U, 256U, 258U}) {
                float sum = 0;
                for (unsigned y = 0; y < size[1]; ++y)
                    for (unsigned x = 0; x < size[0]; ++x) {
                        const float x0 = static_cast<float>(x) * 16 / static_cast<float>(size[0]);
                        const float y0 = static_cast<float>(y) * 24 / static_cast<float>(size[1]);
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
