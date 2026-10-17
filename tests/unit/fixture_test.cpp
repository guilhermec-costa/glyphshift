#include <array>
#include <cmath>
#include <glyphshift/core/fixture.hpp>
#include <iostream>
#include <stdexcept>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

template <std::size_t N>
void rejects(const std::array<std::string_view, N>& args, bool vulkan = true) {
    try {
        (void)glyphshift::parse_fixture_options(args, vulkan);
        check(false, "invalid options accepted");
    } catch (const std::invalid_argument&) {
    }
}
}  // namespace

int main() {
    using namespace std::literals;
    using namespace glyphshift;
    const auto defaults = parse_fixture_options({}, false);
    check(defaults.frames == 0 && !defaults.fixed_time && !defaults.validation && !defaults.help,
        "defaults");
    check(fixture_time(0, defaults) == 0 && fixture_time(60, defaults) == 1,
        "fixed simulation step");
    check(std::abs(fixture_time(1, defaults) - 1.0F / 60.0F) < 0.000001F, "first frame");
    const auto opts = parse_fixture_options(
        std::array{"--frames"sv, "120"sv, "--fixed-time"sv, "2.5"sv, "--validation"sv},
        true);
    check(opts.frames == 120 && opts.validation, "valid options");
    check(fixture_time(0, opts) == 2.5F && fixture_time(9999, opts) == 2.5F,
        "fixed time ignores frame");
    check(parse_fixture_options(std::array{"--help"sv}, false).help, "help");
    for (const auto value : {"0"sv, "-1"sv, "1.2"sv, "12x"sv, ""sv, "18446744073709551616"sv})
        rejects(std::array{"--frames"sv, value});
    for (const auto value : {"-1"sv, "nan"sv, "inf"sv, "2x"sv, ""sv, "1e999"sv})
        rejects(std::array{"--fixed-time"sv, value});
    rejects(std::array{"--frames"sv});
    rejects(std::array{"--fixed-time"sv});
    rejects(std::array{"--unknown"sv});
    rejects(std::array{"--validation"sv}, false);
    std::cout << (failures ? "Core tests failed\n" : "All core tests passed\n");
    return failures ? 1 : 0;
}
