#pragma once
#include <windows.h>

#include <glyphshift/editor/editor.hpp>

// The editor's Win32 input surface. Insert and F10 are hotkeys of the overlay
// window (RegisterHotKey), registered only while the target, or the open
// editor, is in the foreground, so other applications keep those keys; like
// the X11 grab, the target does not receive them meanwhile. While the editor
// is open the overlay stops being input-transparent and takes the foreground,
// so keyboard and pointer go to the editor at the target's client
// coordinates; closing returns the foreground to the target once the keys and
// buttons the editor received are released. Losing the foreground closes the
// editor. Nothing is installed in the target process.
//
// GLYPHSHIFT_OVERLAY_NO_FOCUS=1 (tests): no hotkeys are registered and the
// foreground is never taken; tests post WM_HOTKEY, keys and clicks to the
// overlay window instead.
namespace glyphshift::display::win32 {
// WM_HOTKEY identifiers on the overlay window.
inline constexpr int insert_hotkey = 1, f10_hotkey = 2;

// The input source for the editor session keyed by `target` (the overlay's
// editor::Frame window id), reading the overlay window's messages.
const editor::InputSource* editor_input(HWND overlay, HWND target);
}  // namespace glyphshift::display::win32
