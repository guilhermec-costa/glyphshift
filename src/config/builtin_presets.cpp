#include <glyphshift/config/presets.hpp>

#include "chalk.hpp"
#include "comic.hpp"
#include "crt.hpp"
#include "digital_rain.hpp"
#include "ink_edges.hpp"
#include "midnight.hpp"
#include "newsprint.hpp"
#include "sepia.hpp"
#include "true_color.hpp"

namespace glyphshift {
std::span<const BuiltinPreset> builtin_presets() noexcept {
    // Light-paper styles first, then dark-paper styles.
    static constexpr std::array<BuiltinPreset, 9> styles{{
        {"art-comic",
            "Comic",
            "Bold scene-colored inks over a vivid watercolor wash.",
            included::comic},
        {"art-sepia",
            "Sepia",
            "Sepia pen etching on cream paper with a soft ink bleed.",
            included::sepia},
        {"art-newsprint",
            "Newsprint",
            "Black ink, letter shading and ASCII outlines on warm white paper.",
            included::newsprint},
        {"art-chalk", "Chalk", "Chalk drawing on a dark green board.", included::chalk},
        {"art-ink-edges",
            "Ink & Edges",
            "Crisp white letters and line art on black; made for dark games.",
            included::ink_edges},
        {"art-true-color",
            "True color",
            "The game's own colors, drawn in ASCII on a dark wash of the scene.",
            included::true_color},
        {"art-midnight",
            "Midnight",
            "Terminal ASCII in saturated scene colors on a near-black sky.",
            included::midnight},
        {"art-crt", "CRT", "Amber phosphor letters with a warm glow.", included::crt},
        {"art-digital-rain",
            "Digital rain",
            "Glowing green binary digits on black.",
            included::digital_rain},
    }};
    return styles;
}

const BuiltinPreset* builtin_preset(std::string_view name) noexcept {
    for (const auto& preset : builtin_presets())
        if (preset.name == name)
            return &preset;
    return nullptr;
}
}  // namespace glyphshift
