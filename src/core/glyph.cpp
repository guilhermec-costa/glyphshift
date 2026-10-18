#include <algorithm>
#include <cmath>
#include <glyphshift/core/glyph.hpp>

namespace glyphshift {
float tone_luminance(float value, float exposure, float contrast, float gamma) noexcept {
    return std::pow(std::clamp((value * exposure - .5F) * contrast + .5F, 0.0F, 1.0F), 1 / gamma);
}

float auto_exposure_factor(float mean, float strength) noexcept {
    return std::clamp(std::pow(exposure_key / std::max(mean, exposure_floor), strength), .5F, 4.0F);
}

float exposure_adaptation(float seconds) noexcept {
    return seconds > 0 ? 1 - std::exp(-seconds / exposure_adaptation_seconds) : 0;
}

float adapt_luminance(float previous, float measured, float rate) noexcept {
    const float a = std::log(std::max(previous, exposure_floor));
    const float b = std::log(std::max(measured, exposure_floor));
    return std::exp(a + (b - a) * std::clamp(rate, 0.0F, 1.0F));
}

int contour_glyph(std::span<const float, 9> v, float strength, float threshold) noexcept {
    const float gx = (-v[0] + v[2] - 2 * v[3] + 2 * v[5] - v[6] + v[8]) / 4;
    const float gy = (-v[0] - 2 * v[1] - v[2] + v[6] + 2 * v[7] + v[8]) / 4;
    const float magnitude = std::hypot(gx, gy);
    const auto [low, high] = std::minmax_element(v.begin(), v.end());
    if (strength <= 0 || magnitude * strength <= threshold || magnitude <= .55F * (*high - *low))
        return -1;
    if (std::abs(gx) > 2.414214F * std::abs(gy))
        return 0;
    if (std::abs(gy) > 2.414214F * std::abs(gx))
        return 1;
    return gx * gy > 0 ? 2 : 3;
}

int contour_vote(std::span<const float, 25> lattice, float strength, float threshold) noexcept {
    std::array<int, 4> votes{};
    for (unsigned y = 1; y < 4; ++y)
        for (unsigned x = 1; x < 4; ++x) {
            std::array<float, 9> neighborhood{};
            for (unsigned k = 0; k < 9; ++k)
                neighborhood[k] = 2 * lattice[(y + k / 3 - 1) * 5 + x + k % 3 - 1];
            const int axis = contour_glyph(neighborhood, strength, threshold);
            if (axis >= 0)
                ++votes[static_cast<unsigned>(axis)];
        }
    const auto winner =
        static_cast<unsigned>(std::max_element(votes.begin(), votes.end()) - votes.begin());
    int second = 0;
    for (unsigned a = 0; a < 4; ++a)
        if (a != winner)
            second = std::max(second, votes[a]);
    return votes[winner] >= 3 && votes[winner] >= 2 * second ? static_cast<int>(winner) : -1;
}

float luminance(float red, float green, float blue) noexcept {
    return red * 0.2126F + green * 0.7152F + blue * 0.0722F;
}

std::uint8_t mapped_glyph(float value, std::span<const std::uint8_t> ramp) noexcept {
    if (ramp.empty())
        return 0;
    if (!std::isfinite(value))
        value = 0;
    const auto index = std::min(
        static_cast<std::size_t>(std::clamp(value, 0.0F, 1.0F) * static_cast<float>(ramp.size())),
        ramp.size() - 1);
    return ramp[index];
}
}  // namespace glyphshift
