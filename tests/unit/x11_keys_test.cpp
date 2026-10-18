#include <iostream>

#include "../../src/display/x11/keys.hpp"

namespace {
int failures = 0;

void check(bool ok, const char* name) {
    if (!ok) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

glyphshift::editor::Input key(unsigned keysym) {
    glyphshift::editor::Input event{glyphshift::editor::Kind::key_down};
    glyphshift::display::x11::translate_keysym(keysym, event);
    return event;
}
}  // namespace

int main() {
    using glyphshift::editor::Key;
    check(key(0xff63).key == Key::insert && !key(0xff63).character, "Insert");
    check(key(0xff1b).key == Key::escape, "Escape");
    check(key(0xffc7).key == Key::f10, "F10");
    check(key(0xffff).key == Key::del && !key(0xffff).character, "Delete has no text");
    check(key(0xff0d).key == Key::enter && key(0xff09).key == Key::tab, "Enter and Tab");
    check(key(0xff51).key == Key::left && key(0xff56).key == Key::page_down, "navigation");
    check(key(0xffe2).key == Key::shift && key(0xffe4).key == Key::control &&
              key(0xffea).key == Key::alt && key(0xffeb).key == Key::super,
        "modifiers");
    check(key('a').key == Key::none && key('a').character == U'a', "letter");
    check(key('Z').character == U'Z' && key('7').character == U'7', "upper case and digit");
    check(key(' ').character == U' ', "space");
    check(key(0xe9).character == U'é', "Latin-1 keysym is its code point");
    check(key(0x010020ac).character == U'€', "Unicode keysym");
    check(key(0x0101f600).character == U'\U0001f600', "Unicode keysym beyond the BMP");
    check(!key(0x6c1).character && key(0x6c1).key == Key::none, "legacy keysym ignored");
    check(!key(0x1f).character && !key(0x7f).character, "control codes are not text");
    return failures ? 1 : 0;
}
