#include <glyphshift/config/presets.hpp>

#include "amber_braille.hpp"
#include "blueprint.hpp"
#include "green_terminal.hpp"
#include "mosaic.hpp"
#include "newsprint.hpp"
#include "soft_color.hpp"

namespace glyphshift {
std::span<const BuiltinPreset> builtin_presets() noexcept {
    static constexpr std::array<BuiltinPreset, 6> styles{{
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
