#include <GL/glx.h>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {
using SwapBuffers = void (*)(Display*, GLXDrawable);
thread_local bool in_swap = false;
std::atomic_flag recursion_reported = ATOMIC_FLAG_INIT;

void log(const char* text, std::size_t size) noexcept {
  // Diagnostics must not throw through the application's graphics API.
  const auto ignored = write(STDERR_FILENO, text, size);
  (void)ignored;
}
template <std::size_t N>
void log(const char (&text)[N]) noexcept {
  log(text, N - 1);
}

struct Dispatch {
  SwapBuffers swap = nullptr;
  bool diagnostic = false;
};

const Dispatch& dispatch() noexcept {
  // C++ local-static initialization resolves RTLD_NEXT once, across threads.
  // The recursion guard is entered before this initializer, avoiding reentry
  // into its initialization lock if symbol resolution itself invokes us.
  static const Dispatch next = []() noexcept {
    Dispatch result;
    const char* setting = std::getenv("GLYPHSHIFT_LOG");
    result.diagnostic = setting && std::strcmp(setting, "1") == 0;
    dlerror();
    void* symbol = dlsym(RTLD_NEXT, "glXSwapBuffers");
    if (dlerror() == nullptr)
      result.swap = reinterpret_cast<SwapBuffers>(symbol);
    if (result.diagnostic) {
      if (result.swap)
        log("glyphshift: GLX pass-through initialized\n");
      else
        log("glyphshift: cannot resolve next glXSwapBuffers; presentation "
            "skipped\n");
    }
    return result;
  }();
  return next;
}

struct SwapGuard {
  SwapGuard() noexcept { in_swap = true; }
  ~SwapGuard() { in_swap = false; }
};
}  // namespace

extern "C" __attribute__((visibility("default"))) void glXSwapBuffers(
    Display* display, GLXDrawable drawable) {
  const int saved_errno = errno;
  if (in_swap) {
    // A broken downstream interposer can call us recursively. Drop only that
    // nested call; do not deadlock or loop indefinitely. Report at most once.
    if (!recursion_reported.test_and_set(std::memory_order_relaxed)) {
      const char* setting = std::getenv("GLYPHSHIFT_LOG");
      if (setting && std::strcmp(setting, "1") == 0)
        log("glyphshift: recursive glXSwapBuffers suppressed\n");
    }
    errno = saved_errno;
    return;
  }
  const SwapGuard guard;
  const auto& next = dispatch();
  errno = saved_errno;
  if (next.swap) next.swap(display, drawable);
}
