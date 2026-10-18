#pragma once
#include <GL/glx.h>
#include <dlfcn.h>

// GLX's GL entry-point loader for the OpenGL renderer's table. RTLD_NEXT
// reaches the real libGL both from the injected library, which interposes
// glXGetProcAddress itself, and from processes that link libGL.
namespace glyphshift::display::x11 {
inline void (*glx_proc(const char* name))() {
    using GetProc = void (*(*)(const GLubyte*))();
    static const GetProc get = [] {
        auto found = reinterpret_cast<GetProc>(dlsym(RTLD_NEXT, "glXGetProcAddressARB"));
        if (!found)
            found = reinterpret_cast<GetProc>(dlsym(RTLD_NEXT, "glXGetProcAddress"));
        return found;
    }();
    return get ? get(reinterpret_cast<const GLubyte*>(name)) : nullptr;
}
}  // namespace glyphshift::display::x11
