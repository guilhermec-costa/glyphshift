#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <glyphshift/config/presets.hpp>
#include <iostream>
#include <thread>

namespace {
void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}
}  // namespace

int main() {
    using namespace glyphshift;
    char pattern[] = "/tmp/glyphshift-presets-XXXXXX";
    const char* root = mkdtemp(pattern);
    if (!root)
        return 1;
    const std::filesystem::path path = root;
    try {
        PresetStore first(path, "game-a"), other(path, "game-b");
        std::string error;
        Settings actual;
        check(builtin_presets().size() == 6 && !builtin_preset("not-included"),
            "six included styles with explicit lookup");
        for (const auto& preset : builtin_presets()) {
            check(first.load(preset.name, actual, error) && error.empty(),
                "included styles load without installed files");
            check(actual == parse_settings(preset.config).value &&
                      !parse_settings(preset.config).warnings && valid_art(actual.art),
                "embedded style contents are valid");
            const auto atlas = build_character_atlas(actual.glyphs);
            check(!atlas.diagnostic[0], "included style atlas has no fallback");
            const auto names = first.names();
            check(std::count(names.begin(), names.end(), preset.name) == 1,
                "included style listed exactly once");
        }
        check(!std::filesystem::exists(first.directory()),
            "browsing included styles does not write to the profile");
        check(first.selected(error) == "default" && error.empty(), "missing default");
        const Settings expected{21, 19, false, false, 0.37F};
        check(first.save("night", expected, error) && first.select("night", error),
            "atomic save/select");
        const auto child = fork();
        check(child >= 0, "fork");
        if (!child) {
            PresetStore restarted(path, "game-a");
            Settings result;
            const bool ok = restarted.selected(error) == "night" &&
                            restarted.load("night", result, error) && result == expected;
            _exit(ok ? 0 : 1);
        }
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "presets survive process restart");
        check(!other.load("night", actual, error) && error.empty(), "per-game isolation");
        check(first.select("art-newsprint", error) && first.selected(error) == "art-newsprint",
            "included style selection remembered");
        check(first.save("art-newsprint", expected, error) &&
                  first.load("art-newsprint", actual, error) && actual == expected,
            "saved personal settings take precedence over included fallback");
        const auto names = first.names();
        check(std::count(names.begin(), names.end(), "art-newsprint") == 1,
            "personal override does not duplicate the included entry");
        check(other.load("art-newsprint", actual, error) && actual.art.illustrated &&
                  actual != expected,
            "personal override does not change the included style for other games");
        std::ofstream(first.directory() / "art-newsprint.conf") << "cell_width=invalid\n";
        check(!first.load("art-newsprint", actual, error) && !error.empty(),
            "broken personal override is reported rather than silently replaced");
        check(serialize_settings(expected).starts_with(
                  "enabled=false\ncell_width=21\ncell_height=19\ncolor_mode="
                  "mono\nglyph_strength=0.37\ncharacter_set=classic\ncharacters=\" "
                  ".:-=+*#%@\"\nglyph_font=\"\"\nglyph_order=manual\nglyph_reverse="
                  "false\n") &&
                  parse_settings(serialize_settings(expected)).value == expected,
            "canonical settings and extended round trip");
        auto custom = expected;
        custom.art.illustrated = true;
        custom.art.fill = false;
        custom.art.contour_overlay = false;
        custom.art.shape_matching = true;
        custom.art.shape_threshold = .31F;
        custom.art.scene_paper = .85F;
        custom.art.exposure = 1.4F;
        custom.art.contrast = 1.2F;
        custom.art.gamma = .8F;
        custom.art.edge_strength = .75F;
        custom.art.edge_threshold = .2F;
        custom.art.foreground = {.1F, .3F, .7F};
        custom.art.background = {.8F, .9F, 1};
        custom.glyphs.set = 6;
        std::strcpy(custom.glyphs.text.data(), " ░▒▓█#\\\"");
        custom.glyphs.density_order = true;
        custom.glyphs.reverse = true;
        check(first.save("characters", custom, error) && first.load("characters", actual, error) &&
                  actual == custom,
            "custom Unicode selection persists");
        auto precise = expected;
        precise.glyph_strength = .123456789F;
        check(first.save("precise", precise, error) && first.load("precise", actual, error) &&
                  actual == precise,
            "fractional strength round-trips without precision loss");
        check(!first.save("bad-strength", Settings{8, 12, true, true, 1.5F}, error),
            "invalid glyph strength rejected");
        for (const auto name : {"", "../escape", "/tmp/escape", ".hidden", "with spaces", "x/y"})
            check(!first.save(name, expected, error), "path names rejected");
        std::ofstream(first.directory() / "broken.conf") << "cell_width=invalid\n";
        check(!first.load("broken", actual, error) && actual == Settings{} && !error.empty(),
            "invalid preset safe defaults");
        std::filesystem::create_symlink(path / "external", first.directory() / "link.conf");
        check(!first.load("link", actual, error) && !error.empty(), "symlink reads rejected");
        check(first.save("link", expected, error) && !std::filesystem::exists(path / "external"),
            "atomic save replaces symlink, never follows it");
        std::ofstream(first.directory() / "large.conf") << std::string(4097, 'x');
        check(!first.load("large", actual, error), "oversized preset rejected");
        std::ofstream(first.directory() / "active") << "../escape\n";
        check(first.selected(error) == "default" && !error.empty(), "invalid selection fallback");
        struct stat file{};
        stat((first.directory() / "night.conf").c_str(), &file);
        check((file.st_mode & 0777) == 0600, "private preset file");
        check(!first.save("invalid", Settings{1, 12, true, true}, error),
            "invalid dimensions rejected");
        for (const auto& entry : std::filesystem::directory_iterator(first.directory()))
            check(!entry.path().filename().string().starts_with(".tmp-"),
                "temporary files cleaned");
        check(profile_id("/one/game") == profile_id("/one/game") &&
                  profile_id("/one/game") != profile_id("/two/game"),
            "stable path profile");
        std::filesystem::remove_all(path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(path);
        return 1;
    }
    return 0;
}
