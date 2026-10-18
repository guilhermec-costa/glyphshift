#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace glyphshift {
// Fixed simulation rate: frame N has the same image on both APIs, regardless
// of GPU speed. This is fixture data, independent of any graphics API.
inline constexpr double fixture_rate = 60.0;

struct FixtureOptions {
    std::uint64_t frames = 0;  // zero means run until closed
    std::optional<float> fixed_time;
    bool validation = false;
    bool help = false;
};

FixtureOptions parse_fixture_options(std::span<const std::string_view> args,
    bool supports_validation);
float fixture_time(std::uint64_t frame, const FixtureOptions& options);
// GLYPHSHIFT_FIXTURE_SCROLL=N: a block texture holds for one second (60
// frames, time for a test to place the window), scrolls left one pixel per
// frame for N frames, then holds, for anchored-grid tests. -1 otherwise.
float fixture_scroll(std::uint64_t frame);
}  // namespace glyphshift
