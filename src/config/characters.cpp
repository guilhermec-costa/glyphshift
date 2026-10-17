#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glyphshift/config/characters.hpp>
#include <glyphshift/core/glyph.hpp>
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

bool valid_characters(const CharacterSelection& selection) noexcept {
    if (selection.set >= character_sets.size() || selection.text.back() || selection.font.back() ||
        selection.edges.back())
        return false;
    Codepoints points;
    if (selection.text[0] || selection.set == 6)
        if (!decode_characters(selection.text.data(), points))
            return false;
    if (!decode_characters(selection.edges.data(), points) || points.count != 4)
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
    std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config(FcInitLoadConfigAndFonts(),
        FcConfigDestroy);
    if (!config)
        throw std::runtime_error("cannot initialize font discovery");
    std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> pattern(FcPatternCreate(),
        FcPatternDestroy);
    if (!pattern)
        throw std::runtime_error("cannot allocate font query");
    FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>("monospace"));
    FcConfigSubstitute(config.get(), pattern.get(), FcMatchPattern);
    FcDefaultSubstitute(pattern.get());
    FcResult result;
    std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> match(
        FcFontMatch(config.get(), pattern.get(), &result),
        FcPatternDestroy);
    FcChar8* path = nullptr;
    if (!match || FcPatternGetString(match.get(), FC_FILE, 0, &path) != FcResultMatch)
        throw std::runtime_error("no monospace font found");
    return reinterpret_cast<const char*>(path);
}

struct Font {
    FT_Library library = nullptr;
    FT_Face face = nullptr;

    ~Font() {
        if (face)
            FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }
};
}  // namespace

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
                    FT_Select_Charmap(font.face, FT_ENCODING_UNICODE) ||
                    FT_Set_Pixel_Sizes(font.face, 0, 20))
                    throw std::runtime_error("cannot load Unicode font; using classic");
            }
            if (!FT_Get_Char_Index(font.face, code) ||
                FT_Load_Char(font.face, code, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) {
                char message[128];
                std::snprintf(message, sizeof(message), "font has no U+%04X; using classic", code);
                throw std::runtime_error(message);
            }
            const auto& bitmap = font.face->glyph->bitmap;
            if (!bitmap.width || !bitmap.rows)
                return;
            if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
                throw std::runtime_error("font needs grayscale outlines; using classic");
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
                    const auto pitch =
                        static_cast<unsigned>(bitmap.pitch >= 0 ? bitmap.pitch : -bitmap.pitch);
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
        Codepoints edges;
        decode_characters(selection.edges.data(), edges);
        for (unsigned e = 0; e < 4; ++e) {
            const auto code = edges.values[e];
            if (code == '|' || code == '-' || code == '/' || code == '\\') {
                for (unsigned y = 0; y < 24; ++y)
                    for (unsigned x = 0; x < 16; ++x) {
                        const float px = (static_cast<float>(x) + .5F) / 16;
                        const float py = (static_cast<float>(y) + .5F) / 24;
                        const bool lit = code == '|' ? std::abs(px - .5F) < .075F
                                         : code == '-'
                                             ? std::abs(py - .5F) < .05F
                                             : std::abs(px - (code == '/' ? 1 - py : py)) < .075F;
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
    return atlas;
}
}  // namespace glyphshift
