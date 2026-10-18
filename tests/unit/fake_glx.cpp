#include <GL/glx.h>
#include <dlfcn.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>

namespace {
std::atomic<unsigned> calls{0};
std::atomic<bool> bad_arguments{false};
}  // namespace

extern "C" unsigned fake_calls() {
    return calls.load();
}

extern "C" bool fake_bad_arguments() {
    return bad_arguments.load();
}

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

// Fake SDL entry points: the interposer must forward arguments and return
// values unchanged, and flush the 2D renderer before presenting it.
namespace {
std::atomic<unsigned> sdl_events{0};
std::atomic<bool> sdl_bad{false};
std::atomic<bool> flushed_before_present{false};
}  // namespace

extern "C" bool fake_sdl_ok() {
    return !sdl_bad.load() && flushed_before_present.load();
}

extern "C" int SDL_GL_SwapWindow(void* window) {
    if (window != reinterpret_cast<void*>(0x1234) || errno != EDOM)
        sdl_bad = true;
    errno = ERANGE;
    return 7;
}

extern "C" int SDL_RenderFlush(void* renderer) {
    if (renderer != reinterpret_cast<void*>(0x5678))
        sdl_bad = true;
    sdl_events = 1;
    return 0;
}

extern "C" int SDL_RenderPresent(void* renderer) {
    if (renderer != reinterpret_cast<void*>(0x5678) || errno != EDOM)
        sdl_bad = true;
    flushed_before_present = sdl_events.load() == 1;
    errno = ERANGE;
    return 9;
}
