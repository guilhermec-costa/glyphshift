#include <GL/glx.h>
#include <dlfcn.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>

namespace {
std::atomic<unsigned> calls{0};
std::atomic<bool> bad_arguments{false};
}  // namespace
extern "C" unsigned fake_calls() { return calls.load(); }
extern "C" bool fake_bad_arguments() { return bad_arguments.load(); }
extern "C" void glXSwapBuffers(Display* display, GLXDrawable drawable) {
  if (display != nullptr || drawable != 42 || errno != EDOM)
    bad_arguments = true;
  ++calls;
  if (std::getenv("GLYPHSHIFT_TEST_RECURSE")) {
    // Return through the first interposer to exercise its nested-call guard.
    using Swap = void (*)(Display*, GLXDrawable);
    auto first = reinterpret_cast<Swap>(dlsym(RTLD_DEFAULT, "glXSwapBuffers"));
    first(display, drawable);
  }
  errno = ERANGE;
}
