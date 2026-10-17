#include <algorithm>
#include <cmath>
#include <glyphshift/core/glyph.hpp>

namespace glyphshift {
float tone_luminance(float value, float exposure, float contrast,
                     float gamma) noexcept {
  return std::pow(
      std::clamp((value * exposure - .5F) * contrast + .5F, 0.0F, 1.0F),
      1 / gamma);
}
int contour_glyph(std::span<const float, 9> v, float strength,
                  float threshold) noexcept {
  const float gx = (-v[0] + v[2] - 2 * v[3] + 2 * v[5] - v[6] + v[8]) / 4;
  const float gy = (-v[0] - 2 * v[1] - v[2] + v[6] + 2 * v[7] + v[8]) / 4;
  const float magnitude = std::hypot(gx, gy);
  const auto [low, high] = std::minmax_element(v.begin(), v.end());
  if (strength <= 0 || magnitude * strength <= threshold ||
      magnitude <= .55F * (*high - *low))
    return -1;
  if (std::abs(gx) > 2.414214F * std::abs(gy)) return 0;
  if (std::abs(gy) > 2.414214F * std::abs(gx)) return 1;
  return gx * gy > 0 ? 2 : 3;
}
float luminance(float red, float green, float blue) noexcept {
  return red * 0.2126F + green * 0.7152F + blue * 0.0722F;
}
std::uint8_t mapped_glyph(float value,
                          std::span<const std::uint8_t> ramp) noexcept {
  if (ramp.empty()) return 0;
  if (!std::isfinite(value)) value = 0;
  const auto index =
      std::min(static_cast<std::size_t>(std::clamp(value, 0.0F, 1.0F) *
                                        static_cast<float>(ramp.size())),
               ramp.size() - 1);
  return ramp[index];
}
}  // namespace glyphshift
