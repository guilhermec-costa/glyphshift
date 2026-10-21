#pragma once
#include <glyphshift/editor/editor.hpp>

// Win32 virtual-key codes to the editor's neutral keys. The text a key
// produces comes from the keyboard layout (ToUnicode) and is passed in.
namespace glyphshift::display::win32 {
// Virtual-key codes used here (winuser.h values, so this header needs no
// windows.h and the translation stays testable on its own).
namespace vk {
inline constexpr unsigned back = 0x08, tab = 0x09, enter = 0x0d, shift = 0x10, control = 0x11,
                          menu = 0x12, escape = 0x1b, page_up = 0x21, page_down = 0x22, end = 0x23,
                          home = 0x24, left = 0x25, up = 0x26, right = 0x27, down = 0x28,
                          insert = 0x2d, del = 0x2e, left_windows = 0x5b, right_windows = 0x5c,
                          f10 = 0x79, left_shift = 0xa0, right_shift = 0xa1, left_control = 0xa2,
                          right_control = 0xa3, left_menu = 0xa4, right_menu = 0xa5;
}  // namespace vk

// `character` is the key's Unicode text, 0 if none; control characters
// (below U+0020, and U+007F) are not text.
inline void translate_virtual_key(unsigned key, char32_t character, editor::Input& event) {
    using editor::Key;
    switch (key) {
        case vk::insert:
            event.key = Key::insert;
            break;
        case vk::escape:
            event.key = Key::escape;
            break;
        case vk::f10:
            event.key = Key::f10;
            break;
        case vk::tab:
            event.key = Key::tab;
            break;
        case vk::enter:
            event.key = Key::enter;
            break;
        case vk::back:
            event.key = Key::backspace;
            break;
        case vk::del:
            event.key = Key::del;
            break;
        case vk::home:
            event.key = Key::home;
            break;
        case vk::end:
            event.key = Key::end;
            break;
        case vk::left:
            event.key = Key::left;
            break;
        case vk::right:
            event.key = Key::right;
            break;
        case vk::up:
            event.key = Key::up;
            break;
        case vk::down:
            event.key = Key::down;
            break;
        case vk::page_up:
            event.key = Key::page_up;
            break;
        case vk::page_down:
            event.key = Key::page_down;
            break;
        case vk::shift:
        case vk::left_shift:
        case vk::right_shift:
            event.key = Key::shift;
            break;
        case vk::control:
        case vk::left_control:
        case vk::right_control:
            event.key = Key::control;
            break;
        case vk::menu:
        case vk::left_menu:
        case vk::right_menu:
            event.key = Key::alt;
            break;
        case vk::left_windows:
        case vk::right_windows:
            event.key = Key::super;
            break;
        default:
            break;
    }
    // Keys with a meaning never carry text (Enter and Tab produce control codes).
    if (event.key == Key::none && character >= 0x20 && character != 0x7f && character <= 0x10ffff &&
        (character < 0xd800 || character > 0xdfff))
        event.character = character;
}
}  // namespace glyphshift::display::win32
