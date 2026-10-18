#include "glx_renderer.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <glyphshift/editor/editor.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "../../render/opengl/gl_api.hpp"
#include "../../render/opengl/injected.hpp"
#include "editor_input.hpp"
#include "glx_loader.hpp"

namespace glyphshift::display::x11 {
namespace {
void diagnostic(const char* text) noexcept {
    const auto ignored = write(STDERR_FILENO, text, std::strlen(text));
    (void)ignored;
}

// Current-context queries, resolved past this library like every GLX call.
struct GlxApi {
    decltype(&glXGetCurrentContext) context = nullptr;
    decltype(&glXGetCurrentDrawable) drawable = nullptr;
    decltype(&glXGetCurrentDisplay) display = nullptr;
    decltype(&glXQueryDrawable) query = nullptr;
};

const GlxApi& glx() {
    static const GlxApi value = [] {
        GlxApi result;
        result.context =
            reinterpret_cast<decltype(result.context)>(dlsym(RTLD_NEXT, "glXGetCurrentContext"));
        result.drawable =
            reinterpret_cast<decltype(result.drawable)>(dlsym(RTLD_NEXT, "glXGetCurrentDrawable"));
        result.display =
            reinterpret_cast<decltype(result.display)>(dlsym(RTLD_NEXT, "glXGetCurrentDisplay"));
        result.query =
            reinterpret_cast<decltype(result.query)>(dlsym(RTLD_NEXT, "glXQueryDrawable"));
        return result;
    }();
    return value;
}

struct Context {
    opengl::InjectedRenderer renderer;
    bool disabled = false;
    std::atomic<bool> bound{true}, retired{false};
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
    const auto& gl = opengl::load_api(glx_proc);
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
        if (!state->renderer.ready())
            state->renderer.probe();
        const auto start = std::chrono::steady_clock::now();
        editor::Frame frame(display,
            drawable,
            width,
            height,
            "OpenGL",
            reinterpret_cast<const char*>(gl.GetString(GL_VERSION)),
            editor_input());
        if (frame.settings.enabled || frame.draw_data)
            state->renderer.draw(drawable, width, height, frame.settings, frame.draw_data);
        if (!frame.settings.enabled)
            state->renderer.forget_history();
        frame.complete(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    } catch (const std::exception& error) {
        if (state) {
            state->disabled = true;
            state->renderer.release();
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
            // All GL deletion happens while the owning context is current.
            state->renderer.release();
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
                state->renderer.release();
                erase(context);
            } else if (!state->bound)
                erase(context);
        }
    } catch (...) {
    }
}
}  // namespace glyphshift::display::x11
