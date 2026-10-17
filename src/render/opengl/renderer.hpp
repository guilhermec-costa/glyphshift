#pragma once
#include <GL/glx.h>

#include <glyphshift/config/settings.hpp>

namespace glyphshift::opengl {
void render(Display* display, GLXDrawable drawable) noexcept;
void before_context_change(GLXContext next) noexcept;
void destroy_context(GLXContext context) noexcept;
}  // namespace glyphshift::opengl
