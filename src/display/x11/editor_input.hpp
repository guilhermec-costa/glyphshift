#pragma once
#include <glyphshift/editor/editor.hpp>

// The editor's X11 input surface: an owned XCB connection per presentation
// window, passive Insert/F10 grabs on its managed client and, while the editor
// is open, an input-only child that receives the keyboard and pointer.
namespace glyphshift::display::x11 {
// Null when GLYPHSHIFT_FIXTURE_NO_FOCUS keeps synthetic fixture tests on the
// application's own Xlib queue.
const editor::InputSource* editor_input();
}  // namespace glyphshift::display::x11
