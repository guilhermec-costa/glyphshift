#include <glyphshift/config/presets.hpp>

#include "amber_braille.hpp"
#include "blueprint.hpp"
#include "chalk.hpp"
#include "comic.hpp"
#include "crt.hpp"
#include "digital_rain.hpp"
#include "green_terminal.hpp"
#include "midnight.hpp"
#include "mosaic.hpp"
#include "newsprint.hpp"
#include "sepia.hpp"
#include "soft_color.hpp"

namespace glyphshift {
std::span<const BuiltinPreset> builtin_presets() noexcept {
    static constexpr std::array<BuiltinPreset, 12> styles{{
        {"art-soft-color",
            "Soft color",
            "Gentle color with more original scene detail.",
            included::soft_color},
        {"art-newsprint",
            "Newsprint",
            "Black ink, letter shading and ASCII outlines on warm white paper.",
            included::newsprint},
        {"art-green-terminal",
            "Green terminal",
            "Green characters and ASCII outlines on a dark background.",
            included::green_terminal},
        {"art-blueprint",
            "Blueprint",
            "Cyan outlines on navy paper; no surface fill.",
            included::blueprint},
        {"art-amber-braille",
            "Amber Braille",
            "Dense amber dots on dark paper.",
            included::amber_braille},
        {"art-mosaic",
            "Mosaic",
            "Colored quadrant blocks instead of letter texture.",
            included::mosaic},
        {"art-comic",
            "Comic",
            "Bold scene-colored inks over a vivid watercolor wash.",
            included::comic},
        {"art-sepia",
            "Sepia",
            "Sepia pen etching on cream paper with a soft ink bleed.",
            included::sepia},
        {"art-chalk", "Chalk", "Chalk drawing on a dark green board.", included::chalk},
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
