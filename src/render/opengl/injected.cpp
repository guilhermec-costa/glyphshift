#include "injected.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "gl_api.hpp"
#include "gl_state.hpp"

namespace glyphshift::opengl {
void InjectedRenderer::release() {
    const auto& gl = api();
    for (auto& [handle, drawable] : drawables_) {
        (void)handle;
        if (drawable.framebuffer)
            gl.DeleteFramebuffers(1, &drawable.framebuffer);
        if (drawable.image)
            gl.DeleteTextures(1, &drawable.image);
        drawable.cells.release();
    }
    drawables_.clear();
    timer_.reset();
    pipeline_.reset();
}

void InjectedRenderer::probe() {
    const auto& gl = api();
    const auto* version = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
    int major = 0, minor = 0;
    if (!version || std::sscanf(version, "%d.%d", &major, &minor) != 2 || major < 3 ||
        (major == 3 && minor < 3))
        throw std::runtime_error("OpenGL 3.3 is required");
    GLint clips = 0, feedback = 0;
    gl.GetIntegerv(GL_MAX_CLIP_DISTANCES, &clips);
    gl.GetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &feedback);
    if (clips < 0 || clips > 32 || feedback < 0 || feedback > 32)
        throw std::runtime_error("unsupported GL limits");
    GLint buffers = 0, extensions = 0;
    gl.GetIntegerv(GL_MAX_DRAW_BUFFERS, &buffers);
    if (buffers < 1 || buffers > 32)
        throw std::runtime_error("unsupported draw-buffer limit");
    draw_buffers_ = static_cast<unsigned>(buffers);
    viewport_array_ = major > 4 || (major == 4 && minor >= 1);
    indexed_blend_ = major >= 4;
    clip_control_ = major > 4 || (major == 4 && minor >= 5);
    query_buffer_ = major > 4 || (major == 4 && minor >= 4);
    gl.GetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    for (GLint i = 0; i < extensions; ++i) {
        const auto* name =
            reinterpret_cast<const char*>(gl.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (name && std::strcmp(name, "GL_ARB_viewport_array") == 0)
            viewport_array_ = true;
        if (name && std::strcmp(name, "GL_ARB_draw_buffers_blend") == 0)
            indexed_blend_ = true;
        if (name && std::strcmp(name, "GL_ARB_clip_control") == 0)
            clip_control_ = true;
        if (name && std::strcmp(name, "GL_ARB_query_buffer_object") == 0)
            query_buffer_ = true;
    }
    if ((viewport_array_ && (!gl.ViewportIndexedf || !gl.ScissorIndexed)) ||
        (clip_control_ && !gl.ClipControl))
        throw std::runtime_error("missing extended state entry point");
    if (indexed_blend_ && (!gl.BlendFuncSeparatei || !gl.BlendEquationSeparatei))
        throw std::runtime_error("missing indexed blend entry point");
    clip_count_ = static_cast<unsigned>(clips);
    tf_bindings_ = static_cast<unsigned>(feedback);
    caps_ready_ = true;
}

void InjectedRenderer::forget_history() noexcept {
    for (auto& [handle, drawable] : drawables_) {
        (void)handle;
        drawable.cells.history = false;
    }
}

bool InjectedRenderer::busy() const {
    const auto& gl = api();
    for (unsigned i = 0; i < tf_bindings_; ++i) {
        GLint binding = 0;
        gl.GetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, i, &binding);
        if (binding)
            return true;  // Conservatively avoid writing app feedback buffers.
    }
    for (const auto query : {GL_SAMPLES_PASSED,
             GL_ANY_SAMPLES_PASSED,
             GL_PRIMITIVES_GENERATED,
             GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,
             GL_TIME_ELAPSED}) {
        GLint active = 0;
        gl.GetQueryiv(static_cast<GLenum>(query), GL_CURRENT_QUERY, &active);
        if (active)
            return true;
    }
    return false;
}

// Copies the default back buffer into this drawable's capture texture.
void InjectedRenderer::capture(Drawable& drawable, unsigned width, unsigned height) {
    const auto& gl = api();
    gl.ActiveTexture(GL_TEXTURE0);
    if (!drawable.image) {
        gl.GenTextures(1, &drawable.image);
        gl.GenFramebuffers(1, &drawable.framebuffer);
        if (!drawable.image || !drawable.framebuffer)
            throw std::runtime_error("GL capture object allocation failed");
    }
    gl.BindTexture(GL_TEXTURE_2D, drawable.image);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (drawable.width != width || drawable.height != height) {
        gl.TexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        drawable.width = width;
        drawable.height = height;
        GLint allocated_width = 0, allocated_height = 0;
        gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &allocated_width);
        gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &allocated_height);
        if (allocated_width != static_cast<GLint>(width) ||
            allocated_height != static_cast<GLint>(height))
            throw std::runtime_error("GL capture allocation failed");
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawable.framebuffer);
    gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        drawable.image,
        0);
    if (gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("OpenGL glyph capture framebuffer incomplete");
    gl.BlitFramebuffer(0,
        0,
        static_cast<GLint>(width),
        static_cast<GLint>(height),
        0,
        0,
        static_cast<GLint>(width),
        static_cast<GLint>(height),
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);
}

void InjectedRenderer::draw(std::uint64_t handle,
    unsigned width,
    unsigned height,
    const Settings& settings,
    ImDrawData* ui) {
    const auto& gl = api();
    if (busy())
        return;
    const GlState restore(gl,
        clip_count_,
        draw_buffers_,
        viewport_array_,
        clip_control_,
        indexed_blend_);
    GLboolean double_buffer = GL_FALSE;
    gl.GetBooleanv(GL_DOUBLEBUFFER, &double_buffer);
    if (!double_buffer)
        return;
    if (!pipeline_)
        pipeline_ = std::make_unique<Pipeline>(Caps{viewport_array_, indexed_blend_});
    if (settings.enabled) {
        if (!timer_ && GpuTimer::requested())
            timer_ = std::make_unique<GpuTimer>("injected OpenGL");
        if (timer_)
            timer_->begin(query_buffer_);
        auto& drawable = drawables_[handle];
        capture(drawable, width, height);
        pipeline_->draw(drawable.cells, drawable.image, width, height, false, settings, 0);
        if (timer_)
            timer_->end();
    }
    if (ui)
        pipeline_->draw_editor(*ui, width, height, 0);
}
}  // namespace glyphshift::opengl
