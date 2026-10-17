#include <charconv>
#include <cmath>
#include <glyphshift/core/fixture.hpp>
#include <stdexcept>
#include <string>

namespace glyphshift {
FixtureOptions parse_fixture_options(std::span<const std::string_view> args,
    bool supports_validation) {
    FixtureOptions result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--help")
            result.help = true;
        else if (arg == "--validation" && supports_validation)
            result.validation = true;
        else if (arg == "--frames" || arg == "--fixed-time") {
            if (++i == args.size())
                throw std::invalid_argument("Missing value for " + std::string(arg));
            const auto value = args[i];
            if (arg == "--frames") {
                std::uint64_t count = 0;
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), count);
                if (error != std::errc{} || end != value.data() + value.size() || count == 0)
                    throw std::invalid_argument("--frames requires a positive integer");
                result.frames = count;
            } else {
                float time = 0;
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), time);
                if (error != std::errc{} || end != value.data() + value.size() ||
                    !std::isfinite(time) || time < 0)
                    throw std::invalid_argument("--fixed-time requires finite nonnegative seconds");
                result.fixed_time = time;
            }
        } else
            throw std::invalid_argument("Unknown option: " + std::string(arg));
    }
    return result;
}

float fixture_time(std::uint64_t frame, const FixtureOptions& options) {
    return options.fixed_time.value_or(
        static_cast<float>(static_cast<double>(frame) / fixture_rate));
}
}  // namespace glyphshift
