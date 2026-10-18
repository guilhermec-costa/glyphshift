#pragma once
#include <GL/glx.h>

// The injected OpenGL renderer bound to GLX: one InjectedRenderer per GLX
// context, the editor frame for each presented drawable, and resource release
// that follows the application's context switches and destruction.
namespace glyphshift::display::x11 {
void render(Display* display, GLXDrawable drawable) noexcept;
// Renders into whatever GLX drawable is current on this thread, if any. Used
// when a toolkit presents through a privately resolved glXSwapBuffers.
void render_current() noexcept;
void before_context_change(GLXContext next) noexcept;
void destroy_context(GLXContext context) noexcept;
}  // namespace glyphshift::display::x11
