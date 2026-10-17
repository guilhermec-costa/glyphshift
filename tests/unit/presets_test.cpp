#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <glyphshift/config/presets.hpp>
#include <iostream>
#include <thread>
namespace {
void check(bool ok, const char* name) {
  if (!ok) throw std::runtime_error(name);
}
}  // namespace
int main() {
  using namespace glyphshift;
  char pattern[] = "/tmp/glyphshift-presets-XXXXXX";
  const char* root = mkdtemp(pattern);
  if (!root) return 1;
  const std::filesystem::path path = root;
  try {
    PresetStore first(path, "game-a"), other(path, "game-b");
    std::string error;
    Settings actual;
    check(first.selected(error) == "default" && error.empty(),
          "missing default");
    const Settings expected{21, 19, false, false, 0.37F};
    check(first.save("night", expected, error) && first.select("night", error),
          "atomic save/select");
    const auto child = fork();
    check(child >= 0, "fork");
    if (!child) {
      PresetStore restarted(path, "game-a");
      Settings result;
      const bool ok = restarted.selected(error) == "night" &&
                      restarted.load("night", result, error) &&
                      result == expected;
      _exit(ok ? 0 : 1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "presets survive process restart");
    check(!other.load("night", actual, error) && error.empty(),
          "per-game isolation");
    check(serialize_settings(expected) ==
              "enabled=false\ncell_width=21\ncell_height=19\ncolor_mode="
              "mono\nglyph_strength=0.37\n",
          "canonical settings");
    auto precise = expected;
    precise.glyph_strength = .123456789F;
    check(first.save("precise", precise, error) &&
              first.load("precise", actual, error) && actual == precise,
          "fractional strength round-trips without precision loss");
    check(!first.save("bad-strength", Settings{8, 12, true, true, 1.5F}, error),
          "invalid glyph strength rejected");
    for (const auto name :
         {"", "../escape", "/tmp/escape", ".hidden", "with spaces", "x/y"})
      check(!first.save(name, expected, error), "path names rejected");
    std::ofstream(first.directory() / "broken.conf") << "cell_width=invalid\n";
    check(!first.load("broken", actual, error) && actual == Settings{} &&
              !error.empty(),
          "invalid preset safe defaults");
    std::filesystem::create_symlink(path / "external",
                                    first.directory() / "link.conf");
    check(!first.load("link", actual, error) && !error.empty(),
          "symlink reads rejected");
    check(first.save("link", expected, error) &&
              !std::filesystem::exists(path / "external"),
          "atomic save replaces symlink, never follows it");
    std::ofstream(first.directory() / "large.conf") << std::string(4097, 'x');
    check(!first.load("large", actual, error), "oversized preset rejected");
    std::ofstream(first.directory() / "active") << "../escape\n";
    check(first.selected(error) == "default" && !error.empty(),
          "invalid selection fallback");
    struct stat file{};
    stat((first.directory() / "night.conf").c_str(), &file);
    check((file.st_mode & 0777) == 0600, "private preset file");
    check(!first.save("invalid", Settings{1, 12, true, true}, error),
          "invalid dimensions rejected");
    for (const auto& entry :
         std::filesystem::directory_iterator(first.directory()))
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
