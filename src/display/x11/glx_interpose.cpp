#include <GL/glx.h>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <glyphshift/config/settings.hpp>

#include "glx_renderer.hpp"

namespace {
using SwapBuffers = void (*)(Display*, GLXDrawable);
thread_local bool in_swap = false;
// Set while a toolkit-level present (SDL) has already rendered this frame.
thread_local bool toolkit_presenting = false;
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
    SwapGuard() noexcept {
        in_swap = true;
    }

    ~SwapGuard() {
        in_swap = false;
    }
};
}  // namespace

extern "C" __attribute__((visibility("default"))) void glXSwapBuffers(Display* display,
    GLXDrawable drawable) {
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
    if (next.swap && !toolkit_presenting)
        glyphshift::display::x11::render(display, drawable);
    errno = saved_errno;
    if (next.swap)
        next.swap(display, drawable);
}

// SDL loads libGL itself and resolves glXSwapBuffers with dlsym on that
// handle, which bypasses symbol interposition. The application, however, calls
// SDL_GL_SwapWindow through its own symbol binding, so render there, then let
// SDL present. SDL2 returns int and SDL3 bool; both come back in the integer
// return register, which is forwarded unchanged.
extern "C" __attribute__((visibility("default"))) int SDL_GL_SwapWindow(void* window) {
    static const auto next =
        reinterpret_cast<int (*)(void*)>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
    if (!next)
        return -1;
    const int saved_errno = errno;
    if (in_swap || toolkit_presenting) {
        errno = saved_errno;
        return next(window);
    }
    static const bool announced = [] {
        const char* setting = std::getenv("GLYPHSHIFT_LOG");
        if (setting && std::strcmp(setting, "1") == 0)
            log("glyphshift: SDL GL presentation interposed\n");
        return true;
    }();
    (void)announced;
    toolkit_presenting = true;
    glyphshift::display::x11::render_current();
    errno = saved_errno;
    const int result = next(window);
    toolkit_presenting = false;
    return result;
}

namespace {
using RendererCall = int (*)(void*);

struct RendererDispatch {
    RendererCall present = nullptr, flush = nullptr;
};

// SDL batches 2D renderer commands until present, so flush them into the GL
// back buffer before capturing it. The flush entry point must come from the
// same library as the present call: SDL2 names it SDL_RenderFlush, SDL3
// SDL_FlushRenderer, and their renderer handles are not interchangeable.
const RendererDispatch& renderer_dispatch() noexcept {
    static const RendererDispatch next = [] {
        RendererDispatch result;
        result.present = reinterpret_cast<RendererCall>(dlsym(RTLD_NEXT, "SDL_RenderPresent"));
        Dl_info present{};
        if (!result.present || !dladdr(reinterpret_cast<void*>(result.present), &present))
            return result;
        for (const char* name : {"SDL_RenderFlush", "SDL_FlushRenderer"}) {
            const auto flush = reinterpret_cast<RendererCall>(dlsym(RTLD_NEXT, name));
            Dl_info owner{};
            if (flush && dladdr(reinterpret_cast<void*>(flush), &owner) &&
                owner.dli_fbase == present.dli_fbase) {
                result.flush = flush;
                break;
            }
        }
        return result;
    }();
    return next;
}
}  // namespace

// The SDL 2D renderer presents inside SDL, where neither glXSwapBuffers nor
// SDL_GL_SwapWindow is reached through interposable symbols. Applications
// call SDL_RenderPresent themselves. SDL2 returns void and SDL3 bool; the
// integer return register is forwarded unchanged. Without a current GLX
// drawable (software, EGL or Vulkan renderers) this only forwards.
extern "C" __attribute__((visibility("default"))) int SDL_RenderPresent(void* renderer) {
    const auto& next = renderer_dispatch();
    if (!next.present)
        return 0;
    const int saved_errno = errno;
    if (in_swap || toolkit_presenting || !next.flush) {
        errno = saved_errno;
        return next.present(renderer);
    }
    static const bool announced = [] {
        const char* setting = std::getenv("GLYPHSHIFT_LOG");
        if (setting && std::strcmp(setting, "1") == 0)
            log("glyphshift: SDL renderer presentation interposed\n");
        return true;
    }();
    (void)announced;
    toolkit_presenting = true;
    next.flush(renderer);
    glyphshift::display::x11::render_current();
    errno = saved_errno;
    const int result = next.present(renderer);
    toolkit_presenting = false;
    return result;
}

extern "C" __attribute__((visibility("default"))) Bool glXMakeCurrent(Display* display,
    GLXDrawable drawable,
    GLXContext context) {
    static const auto next =
        reinterpret_cast<decltype(&glXMakeCurrent)>(dlsym(RTLD_NEXT, "glXMakeCurrent"));
    if (!next)
        return False;
    const int saved_errno = errno;
    glyphshift::display::x11::before_context_change(context);
    errno = saved_errno;
    return next(display, drawable, context);
}

extern "C" __attribute__((visibility("default"))) Bool glXMakeContextCurrent(Display* display,
    GLXDrawable draw,
    GLXDrawable read,
    GLXContext context) {
    static const auto next = reinterpret_cast<decltype(&glXMakeContextCurrent)>(
        dlsym(RTLD_NEXT, "glXMakeContextCurrent"));
    if (!next)
        return False;
    const int saved_errno = errno;
    glyphshift::display::x11::before_context_change(context);
    errno = saved_errno;
    return next(display, draw, read, context);
}

extern "C" __attribute__((visibility("default"))) void glXDestroyContext(Display* display,
    GLXContext context) {
    static const auto next =
        reinterpret_cast<decltype(&glXDestroyContext)>(dlsym(RTLD_NEXT, "glXDestroyContext"));
    if (!next)
        return;
    const int saved_errno = errno;
    glyphshift::display::x11::destroy_context(context);
    errno = saved_errno;
    next(display, context);
}

namespace {
__GLXextFuncPtr procedure(const GLubyte* name, const char* entry) {
    using Get = __GLXextFuncPtr (*)(const GLubyte*);
    const auto next = reinterpret_cast<Get>(dlsym(RTLD_NEXT, entry));
    if (!name || !next)
        return nullptr;
    const auto original = next(name);
    if (!original)
        return nullptr;
#define PROC(function)                                                    \
    if (std::strcmp(reinterpret_cast<const char*>(name), #function) == 0) \
    return reinterpret_cast<__GLXextFuncPtr>(&function)
    PROC(glXGetProcAddress);
    PROC(glXGetProcAddressARB);
    PROC(glXSwapBuffers);
    PROC(glXMakeCurrent);
    PROC(glXMakeContextCurrent);
    PROC(glXDestroyContext);
#undef PROC
    return original;
}
}  // namespace

extern "C" __attribute__((visibility("default"))) __GLXextFuncPtr glXGetProcAddressARB(
    const GLubyte* name) {
    return procedure(name, "glXGetProcAddressARB");
}

extern "C" __attribute__((visibility("default"))) __GLXextFuncPtr glXGetProcAddress(
    const GLubyte* name) {
    return procedure(name, "glXGetProcAddress");
}
