#include <algorithm>
#include <cstring>
#include <glyphshift/config/presets.hpp>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/core/glyph.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
std::uint64_t ink(const glyphshift::CharacterAtlas& atlas, unsigned g) {
  std::uint64_t sum = 0;
  for (unsigned y = 0; y < 24; ++y)
    for (unsigned x = 0; x < 16; ++x)
      sum +=
          atlas.pixels[((g / 16) * 24 + y) * glyphshift::character_atlas_width +
                       (g % 16) * 16 + x];
  return sum;
}
}  // namespace
int main() {
  using namespace glyphshift;
  try {
    Codepoints points;
    check(decode_characters(" .é░⣿🐧", points) && points.count == 6 &&
              points.values[5] == 0x1f427,
          "UTF-8 decodes scalars, not bytes");
    for (const auto& value :
         {std::string(""), std::string("\xc0\x80"), std::string("\xed\xa0\x80"),
          std::string("\xf4\x90\x80\x80"), std::string("\xe2\x96"),
          std::string("x\ny"), std::string(257, 'a')})
      check(!decode_characters(value, points),
            "invalid UTF-8/control/count rejected");
    check(
        decode_characters(std::string(256, 'a'), points) && points.count == 256,
        "256 characters accepted");
    CharacterSelection selection;
    const auto original = build_character_atlas(selection);
    check(original.count == 10 && !original.diagnostic[0],
          "classic built-in remains available without fonts");
    for (unsigned g = 0; g < 10; ++g)
      for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 16; ++x)
          check(original.pixels[((g / 16) * 24 + y) * character_atlas_width +
                                (g % 16) * 16 + x] ==
                    default_atlas[(y / 3) * atlas_width + g * 8 + x / 2],
                "classic scaled without changing its mask");
    selection.set = 4;
    std::strcpy(selection.font.data(), "/missing/font.ttf");
    auto atlas = build_character_atlas(selection);
    check(atlas.count == 5 && !atlas.diagnostic[0] && ink(atlas, 0) == 0 &&
              ink(atlas, 4) == 16 * 24 * 255,
          "block ramp is font independent");
    for (unsigned i = 1; i < atlas.count; ++i)
      check(ink(atlas, i) > ink(atlas, i - 1), "block coverage increases");
    selection.set = 5;
    selection.density_order = true;
    atlas = build_character_atlas(selection);
    check(atlas.count == 256 && !atlas.diagnostic[0] && atlas.ramp[0] == 0 &&
              atlas.ramp[255] == 255,
          "all Braille cells represented");
    for (unsigned i = 1; i < atlas.count; ++i)
      check(ink(atlas, atlas.ramp[i]) >= ink(atlas, atlas.ramp[i - 1]),
            "density ordering");
    selection.reverse = true;
    const auto reversed = build_character_atlas(selection);
    for (unsigned i = 0; i < 256; ++i)
      check(reversed.ramp[i] == atlas.ramp[255 - i], "reverse order");
    selection = {};
    selection.set = 1;
    selection.density_order = true;
    atlas = build_character_atlas(selection);
    check(atlas.count == 95 && !atlas.diagnostic[0],
          "complete printable ASCII from system font");
    selection.set = 6;
    std::strcpy(selection.text.data(), " 01ABCé#\\\"");
    const auto serialized =
        serialize_settings(Settings{8, 12, true, true, .45F, selection});
    const auto parsed = parse_settings(serialized);
    check(!parsed.warnings && parsed.value.glyphs == selection,
          "quoted UTF-8/backslash/quote/hash preset round trip");
    check(parse_settings("characters=\" #@ \" # comment\n").value.glyphs.text ==
              std::array<char, 1025>{" #@ "},
          "quoted spaces and hash retained");
    check(parse_settings("characters=\"01\"\n").value.glyphs.set == 6,
          "characters implies custom selection");
    check(!(parse_settings("character_set=ascii\ncharacters=\"\"\n").warnings &
            invalid_characters),
          "empty dormant custom draft does not prevent built-in selection");
    check(parse_settings("character_set=ascii\n").value.glyphs.density_order,
          "ASCII defaults to density order");
    check(!parse_settings("character_set=ascii\nglyph_order=manual\n")
               .value.glyphs.density_order,
          "manual override respected");
    for (const auto value :
         {"character_set=bad", "characters=\"\"", "characters=\"unterminated",
          "glyph_font=relative.ttf", "glyph_order=bad", "glyph_reverse=bad"}) {
      const auto bad = parse_settings(value);
      check((bad.warnings & invalid_characters) &&
                bad.value.glyphs == CharacterSelection{},
            "invalid selection safely defaults");
    }
    std::strcpy(selection.font.data(), "/missing/font.ttf");
    atlas = build_character_atlas(selection);
    check(atlas.count == 10 && atlas.diagnostic[0],
          "missing font falls back, does not terminate app");
    std::cout << "UTF-8, font atlas, procedural blocks/Braille, ordering and "
                 "config passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
