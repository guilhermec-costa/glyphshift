#include <iostream>

#include "../../src/display/win32/keys.hpp"

// The counterpart of the X11 keysym test for Win32 virtual keys and text.
namespace {
int failures = 0;

void check(bool ok, const char* name) {
    if (!ok) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

glyphshift::editor::Input key(unsigned virtual_key, char32_t character = 0) {
    glyphshift::editor::Input event{glyphshift::editor::Kind::key_down};
    glyphshift::display::win32::translate_virtual_key(virtual_key, character, event);
    return event;
}
}  // namespace

int main() {
    using glyphshift::editor::Key;
    check(key(0x2d).key == Key::insert && !key(0x2d).character, "Insert");
    check(key(0x1b, 0x1b).key == Key::escape && !key(0x1b, 0x1b).character, "Escape has no text");
    check(key(0x79).key == Key::f10, "F10");
    check(key(0x2e).key == Key::del && !key(0x2e, 0x7f).character, "Delete has no text");
    check(key(0x0d, U'\r').key == Key::enter && !key(0x0d, U'\r').character &&
              key(0x09, U'\t').key == Key::tab && !key(0x09, U'\t').character,
        "Enter and Tab");
    check(key(0x08, 0x08).key == Key::backspace && !key(0x08, 0x08).character, "Backspace");
    check(key(0x25).key == Key::left && key(0x22).key == Key::page_down &&
              key(0x24).key == Key::home && key(0x23).key == Key::end,
        "navigation");
    check(key(0xa0).key == Key::shift && key(0x10).key == Key::shift &&
              key(0xa3).key == Key::control && key(0x12).key == Key::alt &&
              key(0x5b).key == Key::super,
        "modifiers, generic and sided");
    check(key(0x41, U'a').key == Key::none && key(0x41, U'a').character == U'a', "letter");
    check(key(0x41, U'A').character == U'A' && key(0x37, U'7').character == U'7',
        "upper case and digit");
    check(key(0x20, U' ').character == U' ', "space");
    check(key(0xde, U'é').character == U'é', "layout character");
    check(key(0x45, U'€').character == U'€', "AltGr character");
    check(key(0x41, U'\U0001f600').character == U'\U0001f600', "character beyond the BMP");
    check(!key(0x41, 0x01).character && !key(0x41, 0x7f).character, "control codes are not text");
    check(!key(0x41, 0xd800).character, "a lone surrogate is not text");
    if (!failures)
        std::cout << "Win32 virtual keys and text translated\n";
    return failures ? 1 : 0;
}
