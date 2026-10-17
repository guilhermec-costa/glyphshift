#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <dlfcn.h>

#include <cstring>
// Test-only next-driver shim. App shader queries remain real; only queries
// resolved through the injected renderer's next GLX procedure loader fail.
namespace {
using Get = __GLXextFuncPtr (*)(const GLubyte*);
void status(GLuint shader, GLenum parameter, GLint* value) {
  if (parameter == GL_COMPILE_STATUS) {
    *value = GL_FALSE;
    return;
  }
  static const auto get =
      reinterpret_cast<Get>(dlsym(RTLD_NEXT, "glXGetProcAddressARB"));
  reinterpret_cast<decltype(&glGetShaderiv)>(
      get(reinterpret_cast<const GLubyte*>("glGetShaderiv")))(shader, parameter,
                                                              value);
}
__GLXextFuncPtr lookup(const GLubyte* name, const char* entry) {
  if (name &&
      std::strcmp(reinterpret_cast<const char*>(name), "glGetShaderiv") == 0)
    return reinterpret_cast<__GLXextFuncPtr>(status);
  const auto next = reinterpret_cast<Get>(dlsym(RTLD_NEXT, entry));
  return next ? next(name) : nullptr;
}
}  // namespace
extern "C" __GLXextFuncPtr glXGetProcAddressARB(const GLubyte* name) {
  return lookup(name, "glXGetProcAddressARB");
}
extern "C" __GLXextFuncPtr glXGetProcAddress(const GLubyte* name) {
  return lookup(name, "glXGetProcAddress");
}
