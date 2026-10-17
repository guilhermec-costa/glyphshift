#include <algorithm>
#include <cmath>
#include <glyphshift/core/glyph.hpp>

namespace glyphshift {
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
