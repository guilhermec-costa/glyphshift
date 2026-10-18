#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glyphshift/config/characters.hpp>
#include <glyphshift/core/glyph.hpp>
#include <glyphshift/platform/fonts.hpp>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

namespace glyphshift {
bool decode_characters(std::string_view text, Codepoints& output) noexcept {
    output = {};
    for (std::size_t at = 0; at < text.size();) {
        if (output.count == output.values.size())
            return false;
        const auto first = static_cast<unsigned char>(text[at++]);
        unsigned extra = 0;
        std::uint32_t value = first, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            extra = 1;
            value = first & 31U;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            extra = 2;
            value = first & 15U;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            extra = 3;
            value = first & 7U;
            minimum = 0x10000;
        } else if (first >= 0x80)
            return false;
        for (unsigned i = 0; i < extra; ++i) {
            if (at == text.size())
                return false;
            const auto next = static_cast<unsigned char>(text[at++]);
            if ((next & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (next & 63U);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            value < 0x20 || (value >= 0x7f && value <= 0x9f))
            return false;
        output.values[output.count++] = value;
    }
    return output.count != 0;
}

namespace {
Codepoints selected(const CharacterSelection& selection);
}  // namespace

bool valid_characters(const CharacterSelection& selection) noexcept {
    if (selection.set >= character_sets.size() || selection.text.back() || selection.font.back() ||
        selection.edges.back() || selection.shapes.back() || !std::isfinite(selection.weight) ||
        selection.weight < 0 || selection.weight > 1)
        return false;
    Codepoints points;
    if (selection.text[0] || selection.set == 6)
        if (!decode_characters(selection.text.data(), points))
            return false;
    if (!decode_characters(selection.edges.data(), points) || points.count != 4)
        return false;
    // Fill and shape-only glyphs share the 256 selectable atlas slots.
    if (selection.shapes[0] && (!decode_characters(selection.shapes.data(), points) ||
                                   points.count + selected(selection).count > 256))
        return false;
    return !selection.font[0] || selection.font[0] == '/';
}

namespace {
void pixel(CharacterAtlas& atlas, unsigned glyph, unsigned x, unsigned y, std::uint8_t value) {
    const unsigned column = glyph % 16, row = glyph / 16;
    atlas.pixels[(row * character_tile_height + y) * character_atlas_width +
                 column * character_tile_width + x] = value;
}

void classic(CharacterAtlas& atlas) {
    atlas.pixels.fill(0);
    atlas.count = static_cast<unsigned>(default_ramp.size());
    for (unsigned g = 0; g < atlas.count; ++g) {
        atlas.ramp[g] = static_cast<std::uint8_t>(g);
        for (unsigned y = 0; y < character_tile_height; ++y)
            for (unsigned x = 0; x < character_tile_width; ++x)
                pixel(atlas, g, x, y, default_atlas[(y / 3) * atlas_width + g * 8 + x / 2]);
    }
}

Codepoints selected(const CharacterSelection& selection) {
    Codepoints points;
    const auto name = character_sets[selection.set];
    if (name == "custom")
        decode_characters(selection.text.data(), points);
    else if (name == "classic")
        decode_characters(default_characters, points);
    else if (name == "binary")
        decode_characters(" 01", points);
    else if (name == "blocks")
        decode_characters(" ░▒▓█", points);
    else if (name == "alphanumeric")
        decode_characters(" .0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
            points);
    else if (name == "ascii")
        for (unsigned c = 32; c <= 126; ++c)
            points.values[points.count++] = c;
    else if (name == "braille")
        for (unsigned c = 0x2800; c <= 0x28ff; ++c)
            points.values[points.count++] = c;
    return points;
}

bool procedural(CharacterAtlas& atlas, unsigned glyph, std::uint32_t code) {
    if (code == 32 || code == 0xa0)
        return true;
    if (code >= 0x2800 && code <= 0x28ff) {
        constexpr unsigned positions[8][2] =
            {{4, 3}, {4, 9}, {4, 15}, {11, 3}, {11, 9}, {11, 15}, {4, 21}, {11, 21}};
        for (unsigned dot = 0; dot < 8; ++dot)
            if ((code - 0x2800) & (1U << dot))
                for (unsigned y = positions[dot][1] - 2; y <= positions[dot][1] + 2; ++y)
                    for (unsigned x = positions[dot][0] - 2; x <= positions[dot][0] + 2; ++x)
                        if ((static_cast<int>(x) - static_cast<int>(positions[dot][0])) *
                                    (static_cast<int>(x) - static_cast<int>(positions[dot][0])) +
                                (static_cast<int>(y) - static_cast<int>(positions[dot][1])) *
                                    (static_cast<int>(y) - static_cast<int>(positions[dot][1])) <=
                            5)
                            pixel(atlas, glyph, x, y, 255);
        return true;
    }
    if (code < 0x2580 || code > 0x259f)
        return false;
    constexpr unsigned quadrants[] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};
    constexpr unsigned bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (unsigned y = 0; y < character_tile_height; ++y)
        for (unsigned x = 0; x < character_tile_width; ++x) {
            bool lit = false;
            if (code == 0x2580)
                lit = y < 12;
            else if (code <= 0x2588)
                lit = y >= 24 - (code - 0x2580) * 3;
            else if (code <= 0x258f)
                lit = x < (0x2590 - code) * 2;
            else if (code == 0x2590)
                lit = x >= 8;
            else if (code <= 0x2593)
                lit = bayer[(y / 3) % 4][(x / 2) % 4] < (code - 0x2590) * 4;
            else if (code == 0x2594)
                lit = y < 3;
            else if (code == 0x2595)
                lit = x >= 14;
            else {
                const unsigned bit = (y >= 12 ? 2U : 0U) + (x >= 8 ? 1U : 0U);
                lit = (quadrants[code - 0x2596] & (1U << bit)) != 0;
            }
            if (lit)
                pixel(atlas, glyph, x, y, 255);
        }
    return true;
}

std::string default_font() {
    // The generic monospace alias often resolves to thin Courier clones whose
    // hairlines vanish at cell scale. Prefer sturdy, distinct-punctuation
    // families in bold, then fall back to whatever monospace is installed.
    constexpr std::string_view families[] = {"DejaVu Sans Mono",
        "Hack",
        "Liberation Mono",
        "Noto Sans Mono",
        "Cascadia Mono",
        "JetBrains Mono",
        "Source Code Pro"};
    return platform::bold_monospace_font(families);
}

struct Font {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    // Shared tile metrics in pixels: every glyph sits on one baseline, so
    // `_`, `-`, `.` and `'` keep their position inside the cell.
    float advance = 16, baseline = 18;

    ~Font() {
        if (face)
            FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }
};
}  // namespace

int shape_glyph(const CharacterAtlas& atlas,
    std::span<const float, 16> levels,
    bool invert) noexcept {
    const auto [low, high] = std::minmax_element(levels.begin(), levels.end());
    const float dark = *low, range = *high - *low;
    if (!(range > 0))
        return -1;
    const unsigned matchable = atlas.ramp[256 * 4] + 1U;
    float best = std::numeric_limits<float>::max();
    int glyph = -1;
    for (unsigned g = 0; g < matchable; ++g) {
        float error = 0;
        for (unsigned r = 0; r < 16 && error < best; ++r) {
            const float target = (levels[r] - dark) / range;
            const float delta =
                static_cast<float>(atlas.ramp[256 * (descriptor_row + r) + g]) / 255 -
                (invert ? 1 - target : target);
            error += delta * delta;
        }
        if (error < best) {
            best = error;
            glyph = static_cast<int>(g);
        }
    }
    return best <= shape_fit * 16 ? glyph : -1;
}

CharacterAtlas build_character_atlas(const CharacterSelection& selection) {
    CharacterAtlas atlas;
    Codepoints fill_points;
    try {
        if (!valid_characters(selection))
            throw std::runtime_error("invalid character selection; using classic");
        const auto points = selected(selection);
        fill_points = points;
        Font font;
        auto render = [&](unsigned g, std::uint32_t code) {
            if (procedural(atlas, g, code))
                return;
            if (!font.face) {
                const std::string path = selection.font[0] ? selection.font.data() : default_font();
                if (FT_Init_FreeType(&font.library) ||
                    FT_New_Face(font.library, path.c_str(), 0, &font.face) ||
                    FT_Select_Charmap(font.face, FT_ENCODING_UNICODE))
                    throw std::runtime_error("cannot load Unicode font; using classic");
                FT_Fixed advance = 0;
                const auto* face = font.face;
                const auto line = static_cast<float>(face->ascender - face->descender);
                if (FT_IS_SCALABLE(face) && line > 0 &&
                    !FT_Get_Advance(font.face,
                        FT_Get_Char_Index(font.face, '0'),
                        FT_LOAD_NO_SCALE,
                        &advance) &&
                    advance > 0) {
                    // Fill the 16x24 tile: the em scale where either the
                    // monospace advance or the ascender..descender line fits.
                    const float units = static_cast<float>(face->units_per_EM);
                    const float pixels =
                        std::min(16 * units / static_cast<float>(advance), 24 * units / line);
                    if (FT_Set_Char_Size(font.face, 0, std::lround(pixels * 64), 72, 72))
                        throw std::runtime_error("cannot scale font; using classic");
                    font.advance = static_cast<float>(advance) * pixels / units;
                    const float ascender = static_cast<float>(face->ascender) * pixels / units;
                    font.baseline = (24 - line * pixels / units) / 2 + ascender;
                } else if (FT_Set_Pixel_Sizes(font.face, 0, 20))
                    throw std::runtime_error("cannot load Unicode font; using classic");
            }
            if (!FT_Get_Char_Index(font.face, code) ||
                FT_Load_Char(font.face, code, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT)) {
                char message[128];
                std::snprintf(message, sizeof(message), "font has no U+%04X; using classic", code);
                throw std::runtime_error(message);
            }
            auto* slot = font.face->glyph;
            if (slot->format == FT_GLYPH_FORMAT_OUTLINE && selection.weight > 0) {
                // Up to two extra tile pixels: one pixel at the common 8x12 cell.
                const auto strength = static_cast<FT_Pos>(std::lround(selection.weight * 2 * 64));
                FT_Outline_EmboldenXY(&slot->outline, strength, strength);
                FT_Outline_Translate(&slot->outline, -strength / 2, -strength / 2);
            }
            if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL))
                throw std::runtime_error("cannot rasterize font glyph; using classic");
            const auto& bitmap = slot->bitmap;
            if (!bitmap.width || !bitmap.rows)
                return;
            if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
                throw std::runtime_error("font needs grayscale outlines; using classic");
            const auto pitch =
                static_cast<unsigned>(bitmap.pitch >= 0 ? bitmap.pitch : -bitmap.pitch);
            if (bitmap.width <= 16 && bitmap.rows <= 24) {
                // Baseline placement, nudged inside the tile only when a
                // glyph's ink overhangs the cell.
                const int left = std::clamp(
                    static_cast<int>(std::lround((16 - font.advance) / 2)) + slot->bitmap_left,
                    0,
                    16 - static_cast<int>(bitmap.width));
                const int top =
                    std::clamp(static_cast<int>(std::lround(font.baseline)) - slot->bitmap_top,
                        0,
                        24 - static_cast<int>(bitmap.rows));
                for (unsigned y = 0; y < bitmap.rows; ++y)
                    for (unsigned x = 0; x < bitmap.width; ++x)
                        pixel(atlas,
                            g,
                            static_cast<unsigned>(left) + x,
                            static_cast<unsigned>(top) + y,
                            bitmap
                                .buffer[(bitmap.pitch >= 0 ? y : bitmap.rows - 1 - y) * pitch + x]);
                return;
            }
            // Wider-than-cell glyphs (for example CJK) are scaled to fit.
            const float scale = std::min({1.0F,
                14.0F / static_cast<float>(bitmap.width),
                22.0F / static_cast<float>(bitmap.rows)});
            const unsigned width =
                std::max(1U, static_cast<unsigned>(static_cast<float>(bitmap.width) * scale));
            const unsigned height =
                std::max(1U, static_cast<unsigned>(static_cast<float>(bitmap.rows) * scale));
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const unsigned sy = std::min(bitmap.rows - 1,
                        static_cast<unsigned>(static_cast<float>(y) / scale));
                    const unsigned sx = std::min(bitmap.width - 1,
                        static_cast<unsigned>(static_cast<float>(x) / scale));
                    const auto row = bitmap.pitch >= 0 ? sy : bitmap.rows - 1 - sy;
                    pixel(atlas,
                        g,
                        (16 - width) / 2 + x,
                        (24 - height) / 2 + y,
                        bitmap.buffer[row * pitch + sx]);
                }
        };
        if (selection.set == 0 && !selection.font[0])
            classic(atlas);
        else {
            atlas.count = points.count;
            for (unsigned g = 0; g < points.count; ++g) {
                atlas.ramp[g] = static_cast<std::uint8_t>(g);
                render(g, points.values[g]);
            }
        }
        Codepoints shapes;
        if (selection.shapes[0]) {
            decode_characters(selection.shapes.data(), shapes);
            for (unsigned s = 0; s < shapes.count; ++s)
                render(atlas.count + s, shapes.values[s]);
            atlas.shapes = shapes.count;
        }
        Codepoints edges;
        decode_characters(selection.edges.data(), edges);
        for (unsigned e = 0; e < 4; ++e) {
            const auto code = edges.values[e];
            if (code == '|' || code == '-' || code == '/' || code == '\\') {
                for (unsigned y = 0; y < 24; ++y)
                    for (unsigned x = 0; x < 16; ++x) {
                        const float px = (static_cast<float>(x) + .5F) / 16;
                        const float py = (static_cast<float>(y) + .5F) / 24;
                        // Four-texel strokes: two solid pixels at an 8x12 cell
                        // instead of one half-intensity pixel.
                        const bool lit = code == '|' ? std::abs(px - .5F) < .125F
                                         : code == '-'
                                             ? std::abs(py - .5F) < .084F
                                             : std::abs(px - (code == '/' ? 1 - py : py)) < .125F;
                        if (lit)
                            pixel(atlas, 256 + e, x, y, 255);
                    }
            } else
                render(256 + e, code);
        }
        if (selection.density_order) {
            std::array<std::uint64_t, 256> coverage{};
            for (unsigned g = 0; g < atlas.count; ++g)
                for (unsigned y = 0; y < 24; ++y)
                    for (unsigned x = 0; x < 16; ++x)
                        coverage[g] += atlas.pixels[((g / 16) * 24 + y) * character_atlas_width +
                                                    (g % 16) * 16 + x];
            std::stable_sort(atlas.ramp.begin(),
                atlas.ramp.begin() + atlas.count,
                [&](auto a, auto b) {
                    return coverage[a] < coverage[b];
                });
        }
        if (selection.reverse)
            std::reverse(atlas.ramp.begin(), atlas.ramp.begin() + atlas.count);
    } catch (const std::exception& error) {
        // Keep contour fallback independent from a missing custom font.
        atlas = build_character_atlas(CharacterSelection{});
        fill_points = selected(CharacterSelection{});
        std::snprintf(atlas.diagnostic.data(),
            atlas.diagnostic.size(),
            "Characters: %s",
            error.what());
    }
    std::array<float, 256> coverage{};
    for (unsigned g = 0; g < 260; ++g) {
        const unsigned bx = (g % 16) * 17, by = (g / 16) * 25;
        for (unsigned y = 0; y < 24; ++y) {
            float row = 0;
            for (unsigned x = 0; x < 16; ++x) {
                row +=
                    atlas.pixels[((g / 16) * 24 + y) * character_atlas_width + (g % 16) * 16 + x];
                atlas.integral[(by + y + 1) * integral_width + bx + x + 1] =
                    row + atlas.integral[(by + y) * integral_width + bx + x + 1];
            }
        }
        if (g < 256)
            coverage[g] = atlas.integral[(by + 24) * integral_width + bx + 16];
    }
    for (unsigned bin = 0; bin < 256; ++bin) {
        const float t = static_cast<float>(bin) / 255;
        unsigned best =
            std::min(static_cast<unsigned>(t * static_cast<float>(atlas.count)), atlas.count - 1);
        if (selection.density_order && !atlas.diagnostic[0]) {
            const float target =
                coverage[atlas.ramp[0]] * (1 - t) + coverage[atlas.ramp[atlas.count - 1]] * t;
            float distance = std::numeric_limits<float>::max();
            for (unsigned i = 0; i < atlas.count; ++i) {
                const float delta = std::abs(coverage[atlas.ramp[i]] - target);
                if (delta < distance) {
                    distance = delta;
                    best = i;
                }
            }
        }
        atlas.ramp[256 + bin] = atlas.ramp[best];
    }
    // Find the closest selected glyph for each four-quadrant silhouette.
    // This is initialization data, not a per-frame search over the atlas.
    std::array<std::array<float, 4>, 256> quadrants{};
    for (unsigned g = 0; g < atlas.count; ++g)
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 0; x < 16; ++x)
                quadrants[g][(y >= 12 ? 2U : 0U) + (x >= 8 ? 1U : 0U)] +=
                    static_cast<float>(atlas.pixels[((g / 16) * 24 + y) * character_atlas_width +
                                                    (g % 16) * 16 + x]) /
                    (255 * 8 * 12);
    for (unsigned pattern = 0; pattern < 16; ++pattern) {
        float best_error = std::numeric_limits<float>::max();
        unsigned best = 0;
        for (unsigned g = 0; g < atlas.count; ++g) {
            float error = 0;
            for (unsigned q = 0; q < 4; ++q) {
                const float target = (pattern & (1U << q)) ? 1.0F : 0.0F;
                const float delta = quadrants[g][q] - target;
                error += delta * delta;
            }
            if (error < best_error) {
                best_error = error;
                best = g;
            }
        }
        atlas.ramp[512 + pattern] = static_cast<std::uint8_t>(best);
    }
    std::array<std::uint8_t, 256> tonal{};
    unsigned tonal_count = 0;
    for (unsigned i = 0; i < atlas.count; ++i) {
        const auto g = atlas.ramp[i];
        const auto code = fill_points.values[g];
        if (code < 0x2580 || code > 0x259f || code == 0x2588)
            tonal[tonal_count++] = g;
    }
    // A blocks-only custom set is still valid and must remain usable.
    if (!tonal_count) {
        tonal_count = atlas.count;
        std::copy_n(atlas.ramp.begin(), tonal_count, tonal.begin());
    }
    for (unsigned bin = 0; bin < 256; ++bin) {
        const float t = static_cast<float>(bin) / 255;
        unsigned best =
            std::min(static_cast<unsigned>(t * static_cast<float>(tonal_count)), tonal_count - 1);
        if (selection.density_order && !atlas.diagnostic[0]) {
            const float target =
                coverage[tonal[0]] * (1 - t) + coverage[tonal[tonal_count - 1]] * t;
            float distance = std::numeric_limits<float>::max();
            for (unsigned i = 0; i < tonal_count; ++i) {
                const float delta = std::abs(coverage[tonal[i]] - target);
                if (delta < distance) {
                    distance = delta;
                    best = i;
                }
            }
        }
        atlas.ramp[768 + bin] = tonal[best];
    }
    // Shape descriptors: mean coverage of each 4x6-pixel region of the 16x24
    // tile, scaled so a glyph's densest region is 1. Matching compares where
    // ink falls, while each matched cell supplies its own two-tone palette.
    const unsigned matchable = atlas.count + atlas.shapes;
    atlas.ramp[256 * 4] = static_cast<std::uint8_t>(matchable - 1);
    for (unsigned g = 0; g < matchable; ++g) {
        std::array<float, 16> region{};
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 0; x < 16; ++x)
                region[(y / 6) * 4 + x / 4] +=
                    atlas.pixels[((g / 16) * 24 + y) * character_atlas_width + (g % 16) * 16 + x];
        const float densest = *std::max_element(region.begin(), region.end());
        for (unsigned r = 0; r < 16; ++r)
            atlas.ramp[256 * (descriptor_row + r) + g] =
                static_cast<std::uint8_t>(densest > 0 ? std::lround(region[r] / densest * 255) : 0);
    }
    return atlas;
}
}  // namespace glyphshift
