#ifdef _WIN32
#include <windows.h>
// After windows.h:
#include <aclapi.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <glyphshift/config/presets.hpp>
#include <iostream>
#include <string_view>
#include <thread>

#include "test_support.hpp"

namespace {
void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}

const glyphshift::Settings expected{21, 19, false, false, 0.37F};

// A later process finds the saved selection and settings.
int read_saved(const std::filesystem::path& root) {
    std::string error;
    glyphshift::PresetStore restarted(root, "game-a");
    glyphshift::Settings result;
    return restarted.selected(error) == "night" && restarted.load("night", result, error) &&
                   result == expected
               ? 0
               : 1;
}

#ifdef _WIN32
std::filesystem::path own_executable() {
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    return path;
}

// Runs this test again as a separate process that only reads the store.
bool restarted_reader(const std::filesystem::path& root) {
    const auto self = own_executable();
    std::wstring command = L"\"" + self.wstring() + L"\" --read-saved \"" + root.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(self.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process))
        return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == 0;
}

// The equivalent of mode 0600: a protected access list (nothing inherited)
// with one entry, allowing the current user.
bool owner_only(const std::filesystem::path& file) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::string user(size, '\0');
    const bool known = GetTokenInformation(token, TokenUser, user.data(), size, &size);
    CloseHandle(token);
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!known || GetNamedSecurityInfoW(file.c_str(),
                      SE_FILE_OBJECT,
                      DACL_SECURITY_INFORMATION,
                      nullptr,
                      nullptr,
                      &dacl,
                      nullptr,
                      &descriptor) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    void* ace = nullptr;
    const bool ok = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                    (control & SE_DACL_PROTECTED) && dacl && dacl->AceCount == 1 &&
                    GetAce(dacl, 0, &ace) &&
                    static_cast<ACE_HEADER*>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                    EqualSid(&static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart,
                        reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
    LocalFree(descriptor);
    return ok;
}
#endif
}  // namespace

int main(int argc, char** argv) {
    using namespace glyphshift;
    if (argc == 3 && std::string_view(argv[1]) == "--read-saved")
        return read_saved(argv[2]);
    const std::filesystem::path path = test::temporary_directory("glyphshift-presets");
    try {
        PresetStore first(path, "game-a"), other(path, "game-b");
        std::string error;
        Settings actual;
        check(builtin_presets().size() == 9 && !builtin_preset("not-included"),
            "nine included styles with explicit lookup");
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
        check(first.save("night", expected, error) && first.select("night", error),
            "atomic save/select");
#ifdef _WIN32
        check(restarted_reader(path), "presets survive process restart");
#else
        const auto child = fork();
        check(child >= 0, "fork");
        if (!child)
            _exit(read_saved(path));
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "presets survive process restart");
#endif
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
        custom.glyphs.weight = .4F;
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
        std::error_code link_error;
        std::filesystem::create_symlink(path / "external",
            first.directory() / "link.conf",
            link_error);
#ifndef _WIN32
        check(!link_error, "symbolic link created for the test");
#endif
        if (!link_error) {
            check(!first.load("link", actual, error) && !error.empty(), "symlink reads rejected");
            check(
                first.save("link", expected, error) && !std::filesystem::exists(path / "external"),
                "atomic save replaces symlink, never follows it");
        } else {
            // Windows creates symbolic links only in Developer Mode or with the privilege.
            std::cout << "SKIPPED: symbolic link checks (" << link_error.message() << ")\n";
        }
        std::ofstream(first.directory() / "large.conf") << std::string(4097, 'x');
        check(!first.load("large", actual, error), "oversized preset rejected");
        std::ofstream(first.directory() / "active") << "../escape\n";
        check(first.selected(error) == "default" && !error.empty(), "invalid selection fallback");
#ifdef _WIN32
        check(owner_only(first.directory() / "night.conf"), "private preset file");
#else
        struct stat file{};
        stat((first.directory() / "night.conf").c_str(), &file);
        check((file.st_mode & 0777) == 0600, "private preset file");
#endif
        check(!first.save("invalid", Settings{1, 12, true, true}, error),
            "invalid dimensions rejected");
        for (const auto& entry : std::filesystem::directory_iterator(first.directory()))
            check(!entry.path().filename().string().starts_with(".tmp-"),
                "temporary files cleaned");
        check(profile_id("/one/game") == profile_id("/one/game") &&
                  profile_id("/one/game") != profile_id("/two/game"),
            "stable path profile");
        // The default store resolves the user's configuration directory and
        // the profile from the environment, else from the executable's path.
        test::set_env(test::config_home_variable, (path / "xdg").string());
        test::set_env("GLYPHSHIFT_PROFILE", "env-game");
        check(PresetStore().directory() == path / "xdg/glyphshift/profiles/env-game",
            "configuration directory and GLYPHSHIFT_PROFILE used");
        test::set_env(test::config_home_variable, "relative");
        test::unset_env("GLYPHSHIFT_PROFILE");
#ifdef _WIN32
        // A relative APPDATA is ignored: the roaming application data folder.
        PWSTR folder = nullptr;
        check(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder) ==
                  S_OK,
            "roaming application data folder");
        const std::filesystem::path roaming = folder;
        CoTaskMemFree(folder);
        const auto own = profile_id(own_executable().string());
        check(
            PresetStore().directory() == roaming / "glyphshift/profiles" / std::string(own.data()),
            "relative APPDATA ignored; profile from the executable");
        test::unset_env(test::config_home_variable);
#else
        test::set_env("HOME", (path / "home").string());
        const auto own = profile_id(std::filesystem::read_symlink("/proc/self/exe").string());
        check(PresetStore().directory() ==
                  path / "home/.config/glyphshift/profiles" / std::string(own.data()),
            "relative XDG_CONFIG_HOME ignored; profile from the executable");
        test::unset_env("XDG_CONFIG_HOME");
        test::unset_env("HOME");
        bool refused = false;
        try {
            PresetStore unavailable;
        } catch (const std::runtime_error&) {
            refused = true;
        }
        check(refused, "no configuration directory refused");
#endif
        std::filesystem::remove_all(path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(path);
        return 1;
    }
    return 0;
}
