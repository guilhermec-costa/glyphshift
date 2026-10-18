#pragma once
#include <GL/glx.h>

#include <glyphshift/config/settings.hpp>

namespace glyphshift::opengl {
void render(Display* display, GLXDrawable drawable) noexcept;
// Renders into whatever GLX drawable is current on this thread, if any. Used
// when a toolkit presents through a privately resolved glXSwapBuffers.
void render_current() noexcept;
void before_context_change(GLXContext next) noexcept;
void destroy_context(GLXContext context) noexcept;
}  // namespace glyphshift::opengl
