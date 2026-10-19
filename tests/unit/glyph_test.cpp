#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/core/glyph.hpp>
#include <iostream>
#include <limits>
#include <string>

#include "test_support.hpp"

namespace {
int failures = 0;

void check(bool value, const char* name) {
    if (!value) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}
}  // namespace

int main() {
    using namespace glyphshift;
    check(std::abs(luminance(1, 1, 1) - 1) < 0.000001F, "white luminance");
    check(luminance(1, 0, 0) == 0.2126F && luminance(0, 1, 0) == 0.7152F &&
              luminance(0, 0, 1) == 0.0722F,
        "shared luminance coefficients");
    check(mapped_glyph(-1, default_ramp) == 0 && mapped_glyph(1, default_ramp) == 9 &&
              mapped_glyph(2, default_ramp) == 9,
        "clamped glyph mapping");
    check(mapped_glyph(std::numeric_limits<float>::quiet_NaN(), default_ramp) == 0,
        "nonfinite mapping");
    const std::array<std::uint8_t, 3> reordered{9, 7, 2};
    check(mapped_glyph(0, reordered) == 9 && mapped_glyph(.5F, reordered) == 7 &&
              mapped_glyph(1, reordered) == 2,
        "ramp is runtime data, not shader control flow");
    check(mapped_glyph(.5F, {}) == 0, "empty ramp safe");
    for (unsigned i = 0; i < 10; ++i)
        check(mapped_glyph((static_cast<float>(i) + .5F) / 10, default_ramp) == i,
            "all glyph bins");
    unsigned previous_density = 0;
    for (unsigned glyph = 0; glyph < glyph_rows.size(); ++glyph) {
        unsigned lit = 0;
        for (unsigned y = 0; y < glyph_size; ++y)
            for (unsigned x = 0; x < glyph_size; ++x) {
                const auto pixel = default_atlas[y * atlas_width + glyph * glyph_size + x];
                check(pixel == 0 || pixel == 255, "binary atlas coverage");
                lit += pixel != 0;
            }
        check(glyph ? lit > 0 : lit == 0, "space empty and all other glyphs visible");
        check(glyph == 0 || lit > previous_density, "ramp ink density strictly increases");
        previous_density = lit;
    }
    check(parse_settings("").value == Settings{} && !parse_settings("").warnings, "defaults");
    const auto valid = parse_settings(
        " # comment\n enabled=false\ncolor_mode=mono # "
        "note\ncell_width=2\ncell_height=128\n");
    check(valid.value == Settings{2, 128, false, false} && !valid.warnings,
        "independent cells and modes");
    check(parse_settings("enabled=0").value.enabled == false &&
              parse_settings("enabled=1").value.enabled,
        "numeric toggle");
    const auto invalid = parse_settings(
        "cell_width=0\ncell_height=129\ncolor_mode=bad\nenabled=bad\nunknown="
        "1\nmalformed");
    check(invalid.value == Settings{} && invalid.warnings == 31,
        "invalid values use safe defaults");
    check(parse_settings("cell_width=16\ncell_width=bad").value.cell_width == 8,
        "invalid duplicate resets to default");
    for (const char* number : {"", "-1", "3.5", "nan", "4294967296", "12x"})
        check(parse_settings(std::string("cell_width=") + number).warnings & invalid_width,
            "invalid numeric syntax");

    check(parse_settings("glyph_strength=0").value.glyph_strength == 0 &&
              parse_settings("glyph_strength=1").value.glyph_strength == 1 &&
              parse_settings("glyph_strength=0.37").value.glyph_strength == .37F,
        "glyph strength accepts endpoints and fractions");
    for (const char* value : {"nan", "inf", "-0.1", "1.1", "", "0.5x"}) {
        const auto parsed = parse_settings(std::string("glyph_strength=") + value);
        check((parsed.warnings & invalid_strength) && parsed.value == Settings{},
            "invalid strength defaults safely");
    }
    const auto directory = test::temporary_directory("glyphshift-config-test");
    const auto path = directory / "live.conf";
    std::ofstream(path).close();
    test::set_env("GLYPHSHIFT_CONFIG", path.string());
    auto replace = [&](const char* contents) {
        auto temporary = path;
        temporary += ".new";
        {
            std::ofstream output(temporary, std::ios::binary);
            output << contents;
        }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
            std::abort();
    };
    replace("cell_width=13\ncell_height=19\ncolor_mode=mono\nenabled=false\n");
    check(runtime_settings() == Settings{13, 19, false, false}, "initial live config");
    for (int i = 0; i < 10; ++i)
        check(runtime_settings() == Settings{13, 19, false, false}, "unchanged file stable");
    replace("glyph_strength=0.37\n");
    check(runtime_settings().glyph_strength == .37F, "strength reloads live");
    replace("enabled=true\n");
    check(runtime_settings() == Settings{}, "atomic file replacement immediately reloaded");
    replace("cell_width=invalid\n");
    check(runtime_settings() == Settings{}, "invalid reload nonfatal");
    std::filesystem::remove(path);
    check(runtime_settings() == Settings{}, "missing file nonfatal");
    std::filesystem::remove_all(directory);
    if (!failures)
        std::cout << "Glyph semantics, atlas, validation and live configuration passed\n";
    return failures ? 1 : 0;
}
