#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "window.hpp"

namespace {
class Renderer {
 public:
  fixture::Window window;
  GLXContext context = nullptr;
  GLuint program = 0, vao = 0;
  ~Renderer() {
    if (context) {
      if (glXMakeCurrent(window.display, window.handle, context)) {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
      }
      glXMakeCurrent(window.display, None, nullptr);
      glXDestroyContext(window.display, context);
    }
  }
  void init() {
    const int attrs[] = {GLX_X_RENDERABLE,
                         True,
                         GLX_DRAWABLE_TYPE,
                         GLX_WINDOW_BIT,
                         GLX_RENDER_TYPE,
                         GLX_RGBA_BIT,
                         GLX_DOUBLEBUFFER,
                         True,
                         GLX_RED_SIZE,
                         8,
                         GLX_GREEN_SIZE,
                         8,
                         GLX_BLUE_SIZE,
                         8,
                         None};
    int count = 0;
    std::unique_ptr<GLXFBConfig, decltype(&XFree)> configs(
        glXChooseFBConfig(window.display, DefaultScreen(window.display), attrs,
                          &count),
        XFree);
    if (!configs || count == 0)
      throw std::runtime_error("No double-buffered GLX visual");
    std::unique_ptr<XVisualInfo, decltype(&XFree)> visual(
        glXGetVisualFromFBConfig(window.display, configs.get()[0]), XFree);
    if (!visual) throw std::runtime_error("No GLX visual");
    window.create(visual.get(), "GlyphShift GLX fixture");
    auto create = reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
        glXGetProcAddressARB(
            reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    if (!create) throw std::runtime_error("GLX_ARB_create_context is required");
    const int context_attrs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                                 3,
                                 GLX_CONTEXT_MINOR_VERSION_ARB,
                                 3,
                                 GLX_CONTEXT_PROFILE_MASK_ARB,
                                 GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                                 None};
    context =
        create(window.display, configs.get()[0], nullptr, True, context_attrs);
    if (!context || !glXMakeCurrent(window.display, window.handle, context))
      throw std::runtime_error(
          "Cannot create/make current an OpenGL 3.3 core context");
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " | "
              << glGetString(GL_RENDERER) << '\n';
    if (std::getenv("GLYPHSHIFT_FIXTURE_GL_DEBUG")) {
      auto callback =
          reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(glXGetProcAddressARB(
              reinterpret_cast<const GLubyte*>("glDebugMessageCallback")));
      if (callback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        callback(
            [](GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar* message,
               const void*) { std::cerr << "GL debug: " << message << '\n'; },
            nullptr);
      }
    }
    // Avoid implicit sRGB conversion: the Vulkan fixture selects UNORM too.
    glDisable(GL_FRAMEBUFFER_SRGB);
    program = glCreateProgram();
    for (const auto type : {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER}) {
      const std::string suffix = type == GL_VERTEX_SHADER ? "vert" : "frag";
      const auto source = fixture::read_text(std::string(FIXTURE_SHADER_DIR) +
                                             "/gradient." + suffix);
      const char* data = source.c_str();
      const GLuint shader = glCreateShader(static_cast<GLenum>(type));
      glShaderSource(shader, 1, &data, nullptr);
      glCompileShader(shader);
      GLint ok = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compilation failed: ") +
                                 log);
      }
      glAttachShader(program, shader);
      glDeleteShader(shader);
    }
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[4096]{};
      glGetProgramInfoLog(program, sizeof(log), nullptr, log);
      throw std::runtime_error(std::string("Program link failed: ") + log);
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glUseProgram(program);
  }
  void run(const glyphshift::FixtureOptions& opts) {
    const auto size = glGetUniformLocation(program, "size");
    const auto time = glGetUniformLocation(program, "time");
    std::uint64_t frame = 0;
    const auto start = std::chrono::steady_clock::now();
    while ((!opts.frames || frame < opts.frames) && window.poll()) {
      if (!window.width || !window.height) continue;
      glViewport(0, 0, static_cast<GLsizei>(window.width),
                 static_cast<GLsizei>(window.height));
      glUniform2f(size, static_cast<float>(window.width),
                  static_cast<float>(window.height));
      glUniform1f(time, glyphshift::fixture_time(frame, opts));
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (const auto error = glGetError(); error != GL_NO_ERROR)
        throw std::runtime_error("OpenGL error: " + std::to_string(error));
      glXSwapBuffers(window.display, window.handle);
      fixture::pace(start, ++frame);
    }
    glFinish();
    std::cout << "GLX fixture: clean shutdown after " << frame << " frames\n";
  }
};
}  // namespace
int main(int argc, char** argv) {
  try {
    const auto opts = fixture::options(argc, argv, false);
    if (opts.help) return 0;
    Renderer renderer;
    renderer.init();
    renderer.run(opts);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "GLX fixture: " << error.what() << '\n';
    return 1;
  }
}
