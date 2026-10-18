#pragma once
#include <span>
#include <string>
#include <string_view>

// Font discovery of the platform layer (Linux: Fontconfig). Rasterization
// stays in the portable core.
namespace glyphshift::platform {
// The font file of the first installed family in `families`, in its bold face
// where it has one, else of the system's default monospace font in bold.
// Throws std::runtime_error when no font is found.
std::string bold_monospace_font(std::span<const std::string_view> families);
}  // namespace glyphshift::platform
