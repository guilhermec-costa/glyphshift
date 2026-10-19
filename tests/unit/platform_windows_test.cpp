#include <windows.h>
// After windows.h:
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <glyphshift/platform/fonts.hpp>
#include <glyphshift/platform/paths.hpp>
#include <glyphshift/platform/process.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "test_support.hpp"

// The Windows platform layer's paths, own executable and font discovery: the
// counterpart of the matching parts of the Linux platform test.
namespace {
void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

// The font file's name, lower case, from discovery's UTF-8 path.
std::wstring file_name(const std::string& path) {
    return lower(std::filesystem::path(std::u8string(path.begin(), path.end())).filename());
}

std::filesystem::path roaming() {
    PWSTR folder = nullptr;
    const bool found =
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder) == S_OK;
    std::filesystem::path path = found ? folder : L"";
    CoTaskMemFree(folder);
    check(found, "roaming application data folder");
    return path;
}
}  // namespace

int main() {
    using namespace glyphshift::platform;
    namespace test = glyphshift::test;
    const auto root = test::temporary_directory("glyphshift-platform-windows");
    try {
        // config_home: an absolute APPDATA, else the roaming folder.
        test::set_env("APPDATA", (root / "config").string());
        check(config_home() == root / "config", "absolute APPDATA used");
        test::set_env("APPDATA", "relative\\config");
        check(config_home() == roaming(), "relative APPDATA ignored");
        test::unset_env("APPDATA");
        check(config_home() == roaming(), "roaming folder without APPDATA");

        const auto self = executable_path();
        check(self.is_absolute() && std::filesystem::is_regular_file(self) &&
                  lower(self.filename()) == L"glyphshift-platform-windows-tests.exe",
            "own executable path");

        // Font discovery. A missing family is not replaced by a substitute:
        // the system's monospace (Consolas Bold) answers, as with no family.
        constexpr std::string_view missing[] = {"GlyphShift No Such Family"};
        const auto fallback = bold_monospace_font({});
        check(std::filesystem::is_regular_file(
                  std::filesystem::path(std::u8string(fallback.begin(), fallback.end()))),
            "default monospace font file");
        check(file_name(fallback) == L"consolab.ttf", "default is Consolas Bold");
        check(bold_monospace_font(missing) == fallback, "missing family falls back to monospace");
        // An installed family with a real bold face is used in bold.
        constexpr std::string_view courier[] = {"GlyphShift No Such Family", "Courier New"};
        check(file_name(bold_monospace_font(courier)) == L"courbd.ttf",
            "first installed family in bold");
        // Lucida Console has no bold face: skipped rather than drawn thin.
        constexpr std::string_view lucida[] = {"Lucida Console"};
        check(bold_monospace_font(lucida) == fallback, "family without bold skipped");
        // Cascadia Mono ships as a variable font: its file's default instance
        // is regular, so it is skipped too.
        constexpr std::string_view cascadia[] = {"Cascadia Mono"};
        if (std::filesystem::exists(L"C:\\Windows\\Fonts\\CascadiaMono.ttf"))
            check(bold_monospace_font(cascadia) == fallback, "variable font skipped");
        else
            std::cout << "SKIPPED: variable font check (Cascadia Mono not installed)\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
    std::cout << "Windows platform: configuration folder, own executable and fonts passed\n";
    return 0;
}
