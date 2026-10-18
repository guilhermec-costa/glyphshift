#pragma once
#include <glyphshift/editor/editor.hpp>

// X11 keysyms to the editor's neutral keys and text, shared by the XCB input
// surface and the Xlib queue filter.
namespace glyphshift::display::x11 {
inline void translate_keysym(unsigned keysym, editor::Input& event) {
    using editor::Key;
    switch (keysym) {
        case 0xff63:
            event.key = Key::insert;
            break;
        case 0xff1b:
            event.key = Key::escape;
            break;
        case 0xffc7:
            event.key = Key::f10;
            break;
        case 0xff09:
            event.key = Key::tab;
            break;
        case 0xff0d:
            event.key = Key::enter;
            break;
        case 0xff08:
            event.key = Key::backspace;
            break;
        case 0xffff:
            event.key = Key::del;
            break;
        case 0xff50:
            event.key = Key::home;
            break;
        case 0xff57:
            event.key = Key::end;
            break;
        case 0xff51:
            event.key = Key::left;
            break;
        case 0xff53:
            event.key = Key::right;
            break;
        case 0xff52:
            event.key = Key::up;
            break;
        case 0xff54:
            event.key = Key::down;
            break;
        case 0xff55:
            event.key = Key::page_up;
            break;
        case 0xff56:
            event.key = Key::page_down;
            break;
        case 0xffe1:
        case 0xffe2:
            event.key = Key::shift;
            break;
        case 0xffe3:
        case 0xffe4:
            event.key = Key::control;
            break;
        case 0xffe9:
        case 0xffea:
            event.key = Key::alt;
            break;
        case 0xffeb:
        case 0xffec:
            event.key = Key::super;
            break;
        default:
            break;
    }
    // Latin-1 keysyms equal their code points; 0x01000000 + U marks Unicode U.
    if ((keysym >= 0x20 && keysym <= 0x7e) || (keysym >= 0xa0 && keysym <= 0xff))
        event.character = keysym;
    else if (keysym >= 0x01000000 && keysym <= 0x0110ffff)
        event.character = keysym & 0xffffff;
}
}  // namespace glyphshift::display::x11
