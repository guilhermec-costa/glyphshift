#include <fontconfig/fontconfig.h>

#include <glyphshift/platform/fonts.hpp>
#include <memory>
#include <stdexcept>

namespace glyphshift::platform {
namespace {
using Pattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;

// The bold match for `family`; with `exact`, only when the match reports that
// family, so Fontconfig's substitutes do not stand in for a missing one.
std::string match(FcConfig* config, const std::string& family, bool exact) {
    Pattern pattern(FcPatternCreate(), FcPatternDestroy);
    if (!pattern)
        throw std::runtime_error("cannot allocate font query");
    FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcPatternAddInteger(pattern.get(), FC_WEIGHT, FC_WEIGHT_BOLD);
    FcConfigSubstitute(config, pattern.get(), FcMatchPattern);
    FcDefaultSubstitute(pattern.get());
    FcResult result;
    Pattern found(FcFontMatch(config, pattern.get(), &result), FcPatternDestroy);
    FcChar8* path = nullptr;
    FcChar8* name = nullptr;
    if (!found || FcPatternGetString(found.get(), FC_FILE, 0, &path) != FcResultMatch)
        return {};
    if (exact &&
        (FcPatternGetString(found.get(), FC_FAMILY, 0, &name) != FcResultMatch ||
            FcStrCmpIgnoreCase(name, reinterpret_cast<const FcChar8*>(family.c_str())) != 0))
        return {};
    return reinterpret_cast<const char*>(path);
}
}  // namespace

std::string bold_monospace_font(std::span<const std::string_view> families) {
    std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config(FcInitLoadConfigAndFonts(),
        FcConfigDestroy);
    if (!config)
        throw std::runtime_error("cannot initialize font discovery");
    for (const auto family : families)
        if (auto path = match(config.get(), std::string(family), true); !path.empty())
            return path;
    if (auto path = match(config.get(), "monospace", false); !path.empty())
        return path;
    throw std::runtime_error("no monospace font found");
}
}  // namespace glyphshift::platform
