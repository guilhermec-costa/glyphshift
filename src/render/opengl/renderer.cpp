#include "renderer.hpp"

#include <dlfcn.h>
#include <imgui.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <glyphshift/core/glyph.hpp>
#include <glyphshift/editor/editor.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "../../display/x11/editor_input.hpp"
#include "gl_api.hpp"
#include "gl_state.hpp"
#include "gpu_timer.hpp"
#include "pipeline.hpp"

namespace glyphshift::opengl {
namespace {
void diagnostic(const char* text) noexcept {
    const auto ignored = write(STDERR_FILENO, text, std::strlen(text));
    (void)ignored;
}

struct Drawable {
    GLuint image = 0, framebuffer = 0;
    unsigned width = 0, height = 0;
    Target cells;
};

struct Context {
    std::unique_ptr<Pipeline> pipeline;
    std::unique_ptr<GpuTimer> timer;
    unsigned clip_count = 0, tf_bindings = 0;
    bool disabled = false, caps_ready = false, viewport_array = false, clip_control = false;
    bool indexed_blend = false, query_buffer = false;
    unsigned draw_buffers = 0;
    std::atomic<bool> bound{true}, retired{false};
    std::unordered_map<GLXDrawable, Drawable> drawables;

    void release() {
        const auto& gl = api();
        for (auto& [handle, drawable] : drawables) {
            (void)handle;
            if (drawable.framebuffer)
                gl.DeleteFramebuffers(1, &drawable.framebuffer);
            if (drawable.image)
                gl.DeleteTextures(1, &drawable.image);
            drawable.cells.release();
        }
        drawables.clear();
        timer.reset();
        pipeline.reset();
    }

    void probe() {
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
        draw_buffers = static_cast<unsigned>(buffers);
        viewport_array = major > 4 || (major == 4 && minor >= 1);
        indexed_blend = major >= 4;
        clip_control = major > 4 || (major == 4 && minor >= 5);
        query_buffer = major > 4 || (major == 4 && minor >= 4);
        gl.GetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint i = 0; i < extensions; ++i) {
            const auto* name =
                reinterpret_cast<const char*>(gl.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (name && std::strcmp(name, "GL_ARB_viewport_array") == 0)
                viewport_array = true;
            if (name && std::strcmp(name, "GL_ARB_draw_buffers_blend") == 0)
                indexed_blend = true;
            if (name && std::strcmp(name, "GL_ARB_clip_control") == 0)
                clip_control = true;
            if (name && std::strcmp(name, "GL_ARB_query_buffer_object") == 0)
                query_buffer = true;
        }
        if ((viewport_array && (!gl.ViewportIndexedf || !gl.ScissorIndexed)) ||
            (clip_control && !gl.ClipControl))
            throw std::runtime_error("missing extended state entry point");
        if (indexed_blend && (!gl.BlendFuncSeparatei || !gl.BlendEquationSeparatei))
            throw std::runtime_error("missing indexed blend entry point");
        clip_count = static_cast<unsigned>(clips);
        tf_bindings = static_cast<unsigned>(feedback);
        caps_ready = true;
    }

    void forget_history() {
        for (auto& [handle, drawable] : drawables) {
            (void)handle;
            drawable.cells.history = false;
        }
    }

    bool busy() const {
        const auto& gl = api();
        for (unsigned i = 0; i < tf_bindings; ++i) {
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
    void capture(Drawable& drawable, unsigned width, unsigned height) {
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

    void draw(GLXDrawable handle,
        unsigned width,
        unsigned height,
        const Settings& settings,
        ImDrawData* ui) {
        const auto& gl = api();
        if (busy())
            return;
        const GlState restore(gl,
            clip_count,
            draw_buffers,
            viewport_array,
            clip_control,
            indexed_blend);
        GLboolean double_buffer = GL_FALSE;
        gl.GetBooleanv(GL_DOUBLEBUFFER, &double_buffer);
        if (!double_buffer)
            return;
        if (!pipeline)
            pipeline = std::make_unique<Pipeline>(Caps{viewport_array, indexed_blend});
        if (settings.enabled) {
            if (!timer && GpuTimer::requested())
                timer = std::make_unique<GpuTimer>("injected OpenGL");
            if (timer)
                timer->begin(query_buffer);
            auto& drawable = drawables[handle];
            capture(drawable, width, height);
            pipeline->draw(drawable.cells, drawable.image, width, height, false, settings, 0);
            if (timer)
                timer->end();
        }
        if (ui)
            pipeline->draw_editor(*ui, width, height, 0);
    }
};

struct Registry {
    std::mutex mutex;
    std::unordered_map<GLXContext, std::shared_ptr<Context>> contexts;
};

Registry& registry() {
    static Registry data;
    return data;
}

std::shared_ptr<Context> find(GLXContext context, bool create) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    const auto found = data.contexts.find(context);
    if (found != data.contexts.end())
        return found->second;
    if (!create)
        return nullptr;
    auto state = std::make_shared<Context>();
    data.contexts.emplace(context, state);
    return state;
}

void erase(GLXContext context) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    data.contexts.erase(context);
}
}  // namespace

void render(Display* display, GLXDrawable drawable) noexcept {
    const auto& x = glx();
    if (!x.context || !x.drawable || !x.display || !x.query)
        return;
    const auto context = x.context();
    if (!context || x.drawable() != drawable || x.display() != display)
        return;
    const auto& gl = api();
    if (!gl.available)
        return;
    std::shared_ptr<Context> state;
    try {
        state = find(context, true);
        state->bound = true;
        if (state->disabled || state->retired) {
            editor::deactivate(display, drawable);
            return;
        }
        unsigned width = 0, height = 0;
        x.query(display, drawable, GLX_WIDTH, &width);
        x.query(display, drawable, GLX_HEIGHT, &height);
        if (!width || !height)
            return;
        if (!state->caps_ready)
            state->probe();
        const auto start = std::chrono::steady_clock::now();
        editor::Frame frame(display,
            drawable,
            width,
            height,
            "OpenGL",
            reinterpret_cast<const char*>(gl.GetString(GL_VERSION)),
            display::x11::editor_input());
        if (frame.settings.enabled || frame.draw_data)
            state->draw(drawable, width, height, frame.settings, frame.draw_data);
        if (!frame.settings.enabled)
            state->forget_history();  // Frames shown unprocessed break continuity.
        frame.complete(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    } catch (const std::exception& error) {
        if (state) {
            state->disabled = true;
            state->release();
        }
        diagnostic("glyphshift: GL context disabled: ");
        diagnostic(error.what());
        editor::report(error.what());
        editor::deactivate(display, drawable);
        diagnostic("\n");
    }
}

void render_current() noexcept {
    const auto& x = glx();
    if (!x.drawable || !x.display)
        return;
    const auto drawable = x.drawable();
    Display* display = x.display();
    if (drawable && display)
        render(display, drawable);
}

void before_context_change(GLXContext next) noexcept {
    const auto& x = glx();
    if (!x.context)
        return;
    const auto current = x.context();
    if (!current || current == next)
        return;
    try {
        if (const auto state = find(current, false)) {
            state->release();  // All GL deletion happens while the owning context is
                               // current.
            state->bound = false;
            if (state->retired)
                erase(current);
        }
    } catch (...) {
    }
}

void destroy_context(GLXContext context) noexcept {
    const auto& x = glx();
    try {
        if (const auto state = find(context, false)) {
            state->retired = true;
            if (x.context && x.context() == context) {
                state->release();
                erase(context);
            } else if (!state->bound)
                erase(context);
        }
    } catch (...) {
    }
}
}  // namespace glyphshift::opengl
