#pragma once
#include <cstdint>

namespace glyphshift::editor::x11 {
// Called with the editor model lock held. Own XCB connections never consume
// the application's Xlib/XCB queue or replace its process-wide error handler.
void poll(void* display, std::uint64_t window, bool& open);
void sync(void* display, std::uint64_t window, bool& open);
void forget(void* display, std::uint64_t window);
}  // namespace glyphshift::editor::x11
