#include "renderer.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <glyphshift/core/glyph.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "gl_api.hpp"
#include "gl_state.hpp"
#include "glyph_shaders.hpp"

namespace glyphshift::opengl {
const GlxApi& glx() {
  static const GlxApi value = [] {
    GlxApi result;
    result.context = reinterpret_cast<decltype(result.context)>(
        dlsym(RTLD_NEXT, "glXGetCurrentContext"));
    result.drawable = reinterpret_cast<decltype(result.drawable)>(
        dlsym(RTLD_NEXT, "glXGetCurrentDrawable"));
    result.display = reinterpret_cast<decltype(result.display)>(
        dlsym(RTLD_NEXT, "glXGetCurrentDisplay"));
    result.query = reinterpret_cast<decltype(result.query)>(
        dlsym(RTLD_NEXT, "glXQueryDrawable"));
    return result;
  }();
  return value;
}
const GlApi& api() {
  static const GlApi value = [] {
    GlApi result;
    using GetProc = void (*(*)(const GLubyte*))();
    auto get =
        reinterpret_cast<GetProc>(dlsym(RTLD_NEXT, "glXGetProcAddressARB"));
    if (!get)
      get = reinterpret_cast<GetProc>(dlsym(RTLD_NEXT, "glXGetProcAddress"));
    if (!get) return result;
    result.available = true;
#define LOAD(member, name)                                   \
  result.member = reinterpret_cast<decltype(result.member)>( \
      get(reinterpret_cast<const GLubyte*>(#name)));         \
  result.available &= result.member != nullptr;
    GLYPHSHIFT_GL_FUNCTIONS(LOAD)
#undef LOAD
    return result;
  }();
  return value;
}
namespace {
void diagnostic(const char* text) noexcept {
  const auto ignored = write(STDERR_FILENO, text, std::strlen(text));
  (void)ignored;
}
struct Drawable {
  GLuint image = 0, framebuffer = 0;
  unsigned width = 0, height = 0;
};
struct Context {
  GLuint program = 0, vao = 0, atlas = 0, ramp = 0;
  GLint size = -1, cell = -1, source_color = -1;
  unsigned clip_count = 0, tf_bindings = 0;
  bool disabled = false, caps_ready = false;
  std::atomic<bool> bound{true}, retired{false};
  std::unordered_map<GLXDrawable, Drawable> drawables;
  void release() {
    const auto& gl = api();
    for (auto& [handle, drawable] : drawables) {
      (void)handle;
      if (drawable.framebuffer) gl.DeleteFramebuffers(1, &drawable.framebuffer);
      if (drawable.image) gl.DeleteTextures(1, &drawable.image);
    }
    drawables.clear();
    if (atlas) gl.DeleteTextures(1, &atlas);
    if (ramp) gl.DeleteTextures(1, &ramp);
    if (vao) gl.DeleteVertexArrays(1, &vao);
    if (program) gl.DeleteProgram(program);
    atlas = ramp = vao = program = 0;
  }
  GLuint shader(GLenum type, const char* source) {
    const auto& gl = api();
    const auto shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[2048]{};
      gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
      diagnostic(log);
      gl.DeleteShader(shader);
      throw std::runtime_error("OpenGL glyph shader failed");
    }
    return shader;
  }
  void probe() {
    const auto& gl = api();
    const auto* version =
        reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
    int major = 0, minor = 0;
    if (!version || std::sscanf(version, "%d.%d", &major, &minor) != 2 ||
        major < 3 || (major == 3 && minor < 3))
      throw std::runtime_error("OpenGL 3.3 is required");
    GLint clips = 0, feedback = 0;
    gl.GetIntegerv(GL_MAX_CLIP_DISTANCES, &clips);
    gl.GetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &feedback);
    if (clips < 0 || clips > 32 || feedback < 0 || feedback > 32)
      throw std::runtime_error("unsupported GL limits");
    clip_count = static_cast<unsigned>(clips);
    tf_bindings = static_cast<unsigned>(feedback);
    caps_ready = true;
  }
  void initialize() {
    const auto& gl = api();
    program = gl.CreateProgram();
    for (const auto type : {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER}) {
      const auto object = shader(
          static_cast<GLenum>(type),
          type == GL_VERTEX_SHADER ? shaders::vertex : shaders::fragment);
      gl.AttachShader(program, object);
      gl.DeleteShader(object);
    }
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[2048]{};
      gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
      diagnostic(log);
      throw std::runtime_error("OpenGL glyph link failed");
    }
    gl.GenVertexArrays(1, &vao);
    gl.GenTextures(1, &atlas);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, atlas);
    texture_parameters(gl);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas_width, atlas_height, 0, GL_RED,
                  GL_UNSIGNED_BYTE, default_atlas.data());
    gl.GenTextures(1, &ramp);
    gl.ActiveTexture(GL_TEXTURE2);
    gl.BindTexture(GL_TEXTURE_2D, ramp);
    texture_parameters(gl);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI,
                  static_cast<GLsizei>(default_ramp.size()), 1, 0,
                  GL_RED_INTEGER, GL_UNSIGNED_BYTE, default_ramp.data());
    gl.UseProgram(program);
    gl.Uniform1i(gl.GetUniformLocation(program, "source_image"), 0);
    gl.Uniform1i(gl.GetUniformLocation(program, "atlas_image"), 1);
    gl.Uniform1i(gl.GetUniformLocation(program, "ramp_image"), 2);
    gl.Uniform1ui(gl.GetUniformLocation(program, "ramp_count"),
                  static_cast<GLuint>(default_ramp.size()));
    size = gl.GetUniformLocation(program, "size");
    cell = gl.GetUniformLocation(program, "cell");
    source_color = gl.GetUniformLocation(program, "source_color");
  }
  static void texture_parameters(const GlApi& gl) {
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  bool busy() const {
    const auto& gl = api();
    for (unsigned i = 0; i < tf_bindings; ++i) {
      GLint binding = 0;
      gl.GetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, i, &binding);
      if (binding)
        return true;  // Conservatively avoid writing app feedback buffers.
    }
    for (const auto query :
         {GL_SAMPLES_PASSED, GL_ANY_SAMPLES_PASSED, GL_PRIMITIVES_GENERATED,
          GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_TIME_ELAPSED}) {
      GLint active = 0;
      gl.GetQueryiv(static_cast<GLenum>(query), GL_CURRENT_QUERY, &active);
      if (active) return true;
    }
    return false;
  }
  void draw(GLXDrawable handle, unsigned width, unsigned height,
            const Settings& settings) {
    const auto& gl = api();
    if (busy()) return;
    const GlState restore(gl, clip_count);
    GLboolean double_buffer = GL_FALSE;
    gl.GetBooleanv(GL_DOUBLEBUFFER, &double_buffer);
    if (!double_buffer) return;
    if (!program) initialize();
    auto& drawable = drawables[handle];
    gl.ActiveTexture(GL_TEXTURE0);
    if (!drawable.image) {
      gl.GenTextures(1, &drawable.image);
      gl.GenFramebuffers(1, &drawable.framebuffer);
    }
    gl.BindTexture(GL_TEXTURE_2D, drawable.image);
    texture_parameters(gl);
    if (drawable.width != width || drawable.height != height) {
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);
      drawable.width = width;
      drawable.height = height;
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawable.framebuffer);
    gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, drawable.image, 0);
    if (gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("OpenGL glyph capture framebuffer incomplete");
    gl.BlitFramebuffer(0, 0, static_cast<GLint>(width),
                       static_cast<GLint>(height), 0, 0,
                       static_cast<GLint>(width), static_cast<GLint>(height),
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.DrawBuffer(GL_BACK);
    gl.Viewport(0, 0, static_cast<GLsizei>(width),
                static_cast<GLsizei>(height));
    gl.UseProgram(program);
    gl.BindVertexArray(vao);
    for (unsigned i = 0; i < 3; ++i) {
      gl.ActiveTexture(GL_TEXTURE0 + i);
      gl.BindTexture(GL_TEXTURE_2D,
                     i == 0 ? drawable.image : (i == 1 ? atlas : ramp));
      gl.BindSampler(i, 0);
    }
    gl.Uniform2f(size, static_cast<float>(width), static_cast<float>(height));
    gl.Uniform2f(cell, static_cast<float>(settings.cell_width),
                 static_cast<float>(settings.cell_height));
    gl.Uniform1ui(source_color, settings.source_color ? 1 : 0);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
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
  if (found != data.contexts.end()) return found->second;
  if (!create) return nullptr;
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
void render(Display* display, GLXDrawable drawable,
            const Settings& settings) noexcept {
  if (!settings.enabled) return;
  const auto& x = glx();
  if (!x.context || !x.drawable || !x.display || !x.query) return;
  const auto context = x.context();
  if (!context || x.drawable() != drawable || x.display() != display) return;
  const auto& gl = api();
  if (!gl.available) return;
  std::shared_ptr<Context> state;
  try {
    state = find(context, true);
    state->bound = true;
    if (state->disabled || state->retired) return;
    unsigned width = 0, height = 0;
    x.query(display, drawable, GLX_WIDTH, &width);
    x.query(display, drawable, GLX_HEIGHT, &height);
    if (!width || !height) return;
    if (!state->caps_ready) state->probe();
    state->draw(drawable, width, height, settings);
  } catch (const std::exception& error) {
    if (state) {
      state->disabled = true;
      state->release();
    }
    diagnostic("glyphshift: GL context disabled: ");
    diagnostic(error.what());
    diagnostic("\n");
  }
}
void before_context_change(GLXContext next) noexcept {
  const auto& x = glx();
  if (!x.context) return;
  const auto current = x.context();
  if (!current || current == next) return;
  try {
    if (const auto state = find(current, false)) {
      state->release();  // All GL deletion happens while the owning context is
                         // current.
      state->bound = false;
      if (state->retired) erase(current);
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
